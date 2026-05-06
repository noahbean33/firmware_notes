use esp_bootloader_esp_idf::MMU_PAGE_SIZE;
use esp_hal::peripherals::SPI0;
use log::warn;

use crate::Error;
use crate::ll;

pub(crate) const SIZE_8KB: u32 = 8 * 1024;
pub(crate) const SIZE_16KB: u32 = 16 * 1024;
pub(crate) const SIZE_32KB: u32 = 32 * 1024;
pub(crate) const SIZE_64KB: u32 = 64 * 1024;

pub(crate) const MMU_FIRST_VADDR: u32 = ll::SOC_DROM_LOW;
pub(crate) const MMU_FINAL_VADDR: u32 = ll::SOC_DROM_HIGH - MMU_PAGE_SIZE;
pub(crate) const MMU_FLASH_MASK: u32 = !(MMU_PAGE_SIZE - 1);

unsafe extern "C" {
    fn ROM_Boot_Cache_Init();
}

pub struct MmuCache {
    spi0: SPI0<'static>,
    l1_autoload_en: bool,
}

impl MmuCache {
    pub fn cache_enable(&mut self) {
        ll::cache_enable(self.l1_autoload_en);
    }

    pub fn cache_disable(&mut self) {
        ll::cache_disable();
    }

    pub fn unmap_all(&mut self) {
        for i in 0..ll::SOC_MMU_ENTRY_NUM {
            ll::mmu_set_entry_invalid(i);
        }
    }

    pub fn map_region(&mut self, vaddr: u32, paddr: u32, mut len: u32) -> Result<(), Error> {
        let offset = get_page_offset(paddr);
        if offset != get_page_offset(vaddr) {
            warn!("[boot] Bad page alignment {:08x} vs {:08x}", paddr, vaddr);
            return Err(Error::BadAlignment);
        }
        len += offset;
        let num_pages = len.div_ceil(MMU_PAGE_SIZE);
        let i_start = get_page(vaddr);
        let val_start = get_page(paddr);
        let i_end = i_start + num_pages;
        let val_end = val_start + num_pages;
        for (i, val) in (i_start..i_end).zip(val_start..val_end) {
            ll::mmu_write_entry(i, val);
        }
        Ok(())
    }

    pub fn new(spi0: SPI0<'static>) -> Self {
        let mut this = Self {
            spi0,
            l1_autoload_en: false,
        };
        // cache_hal_init
        this.l1_autoload_en = ll::is_l1_autoload_en();
        this.cache_enable();
        ll::cache_enable_bus();

        // mmu_hal_init
        unsafe {
            ROM_Boot_Cache_Init();
        }
        #[cfg(any(esp32c6, esp32c2, esp32h2))]
        ll::mmu_set_page_size();
        this.unmap_all();

        this.cs_timing_config();

        this
    }

    fn cs_timing_config(&mut self) {
        let regs = self.spi0.register_block();
        regs.user().modify(|_, w| {
            w.cs_hold().set_bit();
            w.cs_setup().set_bit()
        });
        regs.ctrl2().modify(|_, w| unsafe {
            w.cs_hold_time().bits(ll::SPI_MEM_CS_HOLD_TIME_V);
            w.cs_setup_time().bits(ll::SPI_MEM_CS_SETUP_TIME_V)
        });
    }
}

fn get_page(addr: u32) -> u32 {
    const SOC_MMU_VADDR_MASK: u32 = (MMU_PAGE_SIZE * ll::SOC_MMU_ENTRY_NUM) - 1;
    const SHIFT: u32 = match MMU_PAGE_SIZE {
        SIZE_8KB => 13,
        SIZE_16KB => 14,
        SIZE_32KB => 15,
        SIZE_64KB => 16,
        _ => 16,
    };
    (addr & SOC_MMU_VADDR_MASK) >> SHIFT
}

pub(crate) fn get_page_offset(addr: u32) -> u32 {
    addr & (MMU_PAGE_SIZE - 1)
}
