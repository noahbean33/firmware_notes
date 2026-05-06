#![no_std]

use esp_bootloader_esp_idf::partitions::{AppPartitionSubType, PARTITION_TABLE_MAX_LEN, PartitionType, read_partition_table};
use esp_storage::FlashStorage;
use log::info;

use crate::{image::{AppEntry, ImageLoader}, mmu_cache::MmuCache};

#[cfg_attr(esp32, path = "ll/esp32.rs")]
#[cfg_attr(esp32c2, path = "ll/esp32c2.rs")]
#[cfg_attr(esp32c3, path = "ll/esp32c3.rs")]
#[cfg_attr(esp32c6, path = "ll/esp32c6.rs")]
#[cfg_attr(esp32h2, path = "ll/esp32h2.rs")]
#[cfg_attr(esp32s2, path = "ll/esp32s2.rs")]
#[cfg_attr(esp32s3, path = "ll/esp32s3.rs")]
mod ll;

mod image;
pub mod mmu_cache;

unsafe extern "C" {
    fn esp_rom_gpio_pad_set_drv(iopad_num: u32, drv: u32);
    fn esp_rom_spiflash_config_param(
        id: u32,
        chip_size: u32,
        block_size: u32,
        sector_size: u32,
        page_size: u32,
        status_mask: u32,
    ) -> u32;
}

fn update_flash_config() {
    const BLOCK_SIZE: u32 = 0x10000;
    const SECTOR_SIZE: u32 = 0x1000;
    const PAGE_SIZE: u32 = 0x100;
    const STATUS_MASK: u32 = 0xFFFF;
    let size_mb = 8; // TODO: read from image header
    unsafe {
        esp_rom_spiflash_config_param(
            0,
            size_mb * 0x100000,
            BLOCK_SIZE,
            SECTOR_SIZE,
            PAGE_SIZE,
            STATUS_MASK,
        );
    }
}

fn configure_spi_pins(drv: u32) {
    unsafe {
        esp_rom_gpio_pad_set_drv(ll::MSPI_IOMUX_PIN_NUM_CLK, drv);
        esp_rom_gpio_pad_set_drv(ll::MSPI_IOMUX_PIN_NUM_MISO, drv);
        esp_rom_gpio_pad_set_drv(ll::MSPI_IOMUX_PIN_NUM_MOSI, drv);
        esp_rom_gpio_pad_set_drv(ll::MSPI_IOMUX_PIN_NUM_CS0, drv);
        esp_rom_gpio_pad_set_drv(ll::MSPI_IOMUX_PIN_NUM_HD, drv);
        esp_rom_gpio_pad_set_drv(ll::MSPI_IOMUX_PIN_NUM_WP, drv);
    }
}

fn init_spi_flash() {
    const ESP_ROM_GPIO_DRV_10MA: u32 = 1;
    configure_spi_pins(ESP_ROM_GPIO_DRV_10MA);
    update_flash_config();
}

pub fn init() {
    ll::hardware_init();
    init_spi_flash();
}

#[derive(Debug)]
#[non_exhaustive]
pub enum Error {
    ReadPartitionTableError,
    NoAppPartitionFound,
    FlashReadError,
    InvalidHeader,
    BadAlignment,
    NoDromSegment,
    NoIromSegment,
    BadChecksum,
}

#[derive(Default)]
pub struct AppPartitions {
    factory: Option<u32>,
    // TODO: add OTA info & image offsets
}

pub fn find_app_partitions(flash: &mut FlashStorage) -> Result<AppPartitions, Error> {
    let mut buf = [0u8; PARTITION_TABLE_MAX_LEN];
    let table = read_partition_table(flash, &mut buf)
        .map_err(|_| Error::ReadPartitionTableError)?;

    info!("[boot] Partition table:");
    info!("[boot] ## Label            Type ST Offset   Length");
    let mut parts = AppPartitions::default();
    for (i, entry) in table.iter().enumerate() {
        info!(
            "[boot] {i:>2} {:<19} {:02} {:02} {:08x} {:08x}",
            entry.label_as_str(),
            entry.raw_type(),
            entry.raw_subtype(),
            entry.offset(),
            entry.len()
        );
        match entry.partition_type() {
            PartitionType::App(t) => match t {
                AppPartitionSubType::Factory => parts.factory = Some(entry.offset()),
                _ => (), //TODO
            }
            _ => (), //TODO
        }
    }
    Ok(parts)
}

pub fn load_app(
    parts: AppPartitions,
    mmucache: &mut MmuCache,
    flash: &mut FlashStorage<'static>,
) -> Result<AppEntry, Error> {
    // TODO: find preferred image
    let offset = parts.factory.ok_or(Error::NoAppPartitionFound)?;
    try_load_partition(offset, mmucache, flash)
}

fn try_load_partition(
    offset: u32,
    mmucache: &mut MmuCache,
    flash: &mut FlashStorage<'static>,
) -> Result<AppEntry, Error> {
    ImageLoader::new(offset, mmucache, flash)?
        .process_segments()?
        .verify()?
        .map_segments()
}