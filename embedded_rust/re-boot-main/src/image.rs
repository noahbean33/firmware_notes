#![allow(unused)]

use core::mem::MaybeUninit;

use embedded_storage::ReadStorage;
use esp_bootloader_esp_idf::MMU_PAGE_SIZE;
use esp_hal::rtc_cntl::SocResetReason;
use esp_hal::system::reset_reason;
use esp_storage::FlashStorage;
use log::{info, warn};

use crate::Error;
use crate::ll::{
    SOC_DROM_HIGH, SOC_DROM_LOW, SOC_IROM_HIGH, SOC_IROM_LOW, SOC_RTC_DRAM_HIGH,
    SOC_RTC_DRAM_LOW, SOC_RTC_IRAM_HIGH, SOC_RTC_IRAM_LOW,
};
use crate::mmu_cache::{MMU_FIRST_VADDR, MMU_FINAL_VADDR, MMU_FLASH_MASK, MmuCache, get_page_offset};

struct ImageReader<'f> {
    start: u32,
    offset: u32,
    mmucache: &'f mut MmuCache,
    flash: &'f mut FlashStorage<'static>,
}
impl<'f> ImageReader<'f> {
    fn new(
        paddr: u32,
        mmucache: &'f mut MmuCache,
        flash: &'f mut FlashStorage<'static>
    ) -> Self {
        Self {
            start: paddr,
            offset: paddr,
            mmucache,
            flash,
        }
    }

    fn length(&self) -> usize {
        (self.offset - self.start) as usize
    }

    fn offset(&self) -> u32 {
        self.offset
    }

    fn read(&mut self, buf: &mut [u8]) -> Result<(), Error> {
        self.flash
            .read(self.offset, buf)
            .map_err(|_| Error::FlashReadError)?;
        self.offset += buf.len() as u32;
        Ok(())
    }

    fn read_as<T>(&mut self) -> Result<T, Error> {
        let mut t = MaybeUninit::<T>::uninit();
        let buf = unsafe {
            core::slice::from_raw_parts_mut(t.as_mut_ptr() as *mut u8, size_of::<T>())
        };
        self.read(buf)?;
        // SAFETY
        // safe for packed C structs & primitives
        Ok(unsafe { t.assume_init() })
    }

    fn mmap(&mut self, s: &Segment) -> Result<&[u32], Error> {
        let Segment { vaddr: _, paddr, length } = *s;
        self.mmucache.cache_disable();
        self.mmucache.unmap_all();
        let vaddr = MMU_FIRST_VADDR + get_page_offset(paddr);
        self.mmucache.map_region(vaddr, paddr, length)?;
        self.mmucache.cache_enable();
        self.offset += length;
        // SAFETY: address & length word-aligned via Segment
        let slice = unsafe {
            core::slice::from_raw_parts(vaddr as *const u32, length as usize / 4)
        };
        Ok(slice)
    }
}

#[repr(C, packed)]
#[doc(alias = "esp_image_header_t")]
struct EspImageHeader {
    magic: u8,
    segment_count: u8,
    /// Flash read mode (esp_image_spi_mode_t)
    flash_mode: u8,
    /// ..4 bits are flash chip size (esp_image_flash_size_t)
    /// 4.. bits are flash frequency (esp_image_spi_freq_t)
    #[doc(alias = "spi_size")]
    #[doc(alias = "spi_speed")]
    flash_config: u8,
    entry: u32,

    // extended header part
    wp_pin: u8,
    clk_q_drv: u8,
    d_cs_drv: u8,
    gd_wp_drv: u8,
    chip_id: u16,
    min_rev: u8,
    /// Minimum chip revision supported by image, in format: major * 100 + minor
    min_chip_rev_full: u16,
    /// Maximal chip revision supported by image, in format: major * 100 + minor
    max_chip_rev_full: u16,
    reserved: [u8; 4],
    append_digest: u8,
}

#[repr(C, packed)]
struct EspSegmentHeader {
    vaddr: u32,
    length: u32,
}

struct Segment {
    vaddr: u32,
    paddr: u32,
    length: u32,
}

impl Segment {
    fn new(reader: &mut ImageReader) -> Result<Self, Error> {
        let header = reader.read_as::<EspSegmentHeader>()?;
        const ESP_IMAGE_MAX_FLASH_ADDR_SIZE: u32 = 0x100_0000;
        if (header.length & 0x3) != 0 || header.length > ESP_IMAGE_MAX_FLASH_ADDR_SIZE {
            warn!("[boot] Bad seg length: {}", { header.length });
            return Err(Error::InvalidHeader);
        }
        if (header.vaddr & 0x3) != 0 {
            warn!("[boot] Bad seg address: {}", { header.vaddr });
            return Err(Error::InvalidHeader);
        }
        Ok(Self {
            vaddr: header.vaddr,
            paddr: reader.offset(),
            length: header.length,
        })
    }

    fn should_map(&self) -> bool {
        match self.vaddr {
            (SOC_DROM_LOW..SOC_DROM_HIGH) => true,
            #[cfg(any(esp32, esp32c2, esp32c3, esp32s2, esp32s3))]
            (SOC_IROM_LOW..SOC_IROM_HIGH) => true,
            _ => false,
        }
    }

    fn should_load(&self) -> bool {
        if self.should_map() {
            return false;
        }
        let load_rtc_mem = reset_reason() != Some(SocResetReason::CoreDeepSleep);
        match self.vaddr {
            0..0x1000_0000 => false,
            (SOC_RTC_DRAM_LOW..SOC_RTC_DRAM_HIGH) => load_rtc_mem,
            #[cfg(any(esp32, esp32c2, esp32c3, esp32s2, esp32s3))]
            (SOC_RTC_IRAM_LOW..SOC_RTC_IRAM_HIGH) => load_rtc_mem,
            _ => true,
        }
    }

    fn action(&self) -> &str {
        if self.should_map() {
            "Map"
        } else if self.should_load() {
            "Load"
        } else {
            "None"
        }
    }
}

pub(crate) struct ImageLoader<'f, T> {
    reader: ImageReader<'f>,
    entry: u32,
    state: T,
}

pub(crate) struct Header {
    segment_count: usize,
}
impl<'f> ImageLoader<'f, Header> {
    pub(crate) fn new(
        paddr: u32,
        mmucache: &'f mut MmuCache,
        flash: &'f mut FlashStorage<'static>,
    ) -> Result<Self, Error> {
        let mut reader = ImageReader::new(paddr, mmucache, flash);
        let header = reader.read_as::<EspImageHeader>()?;
        // verify_image_header
        const ESP_IMAGE_HEADER_MAGIC: u8 = 0xE9;
        if header.magic != ESP_IMAGE_HEADER_MAGIC {
            warn!("[boot] Bad magic: {}", { header.magic });
            return Err(Error::InvalidHeader);
        }
        const ESP_IMAGE_MAX_SEGMENTS: usize = 16;
        if header.segment_count as usize > ESP_IMAGE_MAX_SEGMENTS {
            warn!("[boot] Bad seg count: {}", { header.segment_count });
            return Err(Error::InvalidHeader);
        }
        Ok(Self {
            reader,
            entry: header.entry,
            state: Header {
                segment_count: header.segment_count as usize,
            }
        })
    }

    fn process_segment(
        &mut self,
        segment: Segment,
        checksum: &mut u32,
        drom: &mut Option<Segment>,
        irom: &mut Option<Segment>,
    ) -> Result<(), Error> {
        let src = self.reader.mmap(&segment)?;
        let dest = unsafe {
            core::slice::from_raw_parts_mut(segment.vaddr as *mut u32, src.len())
        };
        let is_loading = segment.should_load();
        for (src_w, dest_w) in src.iter().zip(dest) {
            *checksum ^= *src_w;
            #[cfg(not(feature = "test-as-app"))]
            if is_loading {
                *dest_w = *src_w;
            }
        }
        if segment.should_map() {
            if drom.is_none() {
                drom.replace(segment);
            } else if irom.is_none() {
                irom.replace(segment);
            }
        }
        Ok(())
    }

    pub(crate) fn process_segments(mut self) -> Result<ImageLoader<'f, Unverified>, Error> {
        const ESP_ROM_CHECKSUM_INITIAL: u32 = 0xEF;
        let mut checksum = ESP_ROM_CHECKSUM_INITIAL;
        let mut drom: Option<Segment> = None;
        let mut irom: Option<Segment> = None;
        for i in 0..self.state.segment_count {
            let segment = Segment::new(&mut self.reader)?;
            info!(
                "[boot] Segment {i}: paddr = {:08X}, vaddr = {:08X}, size = {:>6}, {}",
                segment.paddr,
                segment.vaddr,
                segment.length,
                segment.action(),
            );
            self.process_segment(segment, &mut checksum, &mut drom, &mut irom)?;
        }
        Ok(ImageLoader {
            reader: self.reader,
            entry: self.entry,
            state: Unverified { checksum, drom, irom }
        })
    }
}

pub(crate) struct Unverified {
    checksum: u32,
    drom: Option<Segment>,
    irom: Option<Segment>,
}
impl<'f> ImageLoader<'f, Unverified> {
    fn verify_checksum(&mut self, checksum: u32) -> Result<(), Error> {
        const PAD_LENGTH: usize = 16;
        let length = self.reader.length();
        let padded_length = (length + PAD_LENGTH) & !15;
        let pad_bytes = padded_length - length;
        let mut buf = [0u8; PAD_LENGTH];
        self.reader.read(&mut buf[0..pad_bytes])?;
        let img_checksum = buf[pad_bytes - 1];
        let calc_checksum =
            ((checksum >> 24) ^ (checksum >> 16) ^ (checksum >> 8) ^ checksum) as u8;
        if calc_checksum != img_checksum {
            warn!("[boot] Bad checksum: calc = {calc_checksum:02X}, image = {img_checksum:02X}");
            return Err(Error::BadChecksum);
        }
        Ok(())
    }

    pub(crate) fn verify(mut self) -> Result<ImageLoader<'f, Verified>, Error> {
        self.verify_checksum(self.state.checksum)?;
        // TODO: verify_hash
        // TODO: verify_signature
        let drom = self.state.drom.ok_or(Error::NoDromSegment)?;
        let irom = self.state.irom.ok_or(Error::NoIromSegment)?;
        Ok(ImageLoader {
            reader: self.reader,
            entry: self.entry,
            state: Verified { drom, irom }
        })
    }
}

pub(crate) struct Verified {
    drom: Segment,
    irom: Segment,
}
impl<'f> ImageLoader<'f, Verified> {
    pub(crate) fn map_segments(self) -> Result<AppEntry, Error> {
        let mmucache = self.reader.mmucache;
        let s = self.state;
        mmucache.cache_disable();
        mmucache.unmap_all();

        mmucache.map_region(s.drom.vaddr, s.drom.paddr, s.drom.length)?;
        mmucache.map_region(s.irom.vaddr, s.irom.paddr, s.irom.length)?;
        mmucache.map_region(
            MMU_FINAL_VADDR,
            s.drom.paddr & MMU_FLASH_MASK,
            MMU_PAGE_SIZE,
        )?;

        mmucache.cache_enable();

        let entry: AppEntry = unsafe {
            core::mem::transmute(self.entry)
        };
        #[cfg(feature = "test-as-app")]
        let entry = app;

        Ok(entry)
    }
}

pub type AppEntry = extern "C" fn() -> !;

#[cfg(feature = "test-as-app")]
#[unsafe(link_section = ".rodata_app")]
static APP_MESSAGE: &str = "[app] Running!";

#[cfg(feature = "test-as-app")]
#[unsafe(link_section = ".text_app")]
pub extern "C" fn app() -> ! {
    loop {
        info!("{APP_MESSAGE}");
        esp_hal::rom::ets_delay_us(2_000_000);
    }
}