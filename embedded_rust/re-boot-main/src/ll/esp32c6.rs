use esp_bootloader_esp_idf::MMU_PAGE_SIZE;
use esp_hal::peripherals::{EXTMEM, SPI0};

use crate::mmu_cache::{SIZE_8KB, SIZE_16KB, SIZE_32KB, SIZE_64KB};

pub(crate) const MSPI_IOMUX_PIN_NUM_CLK: u32 = 29;
pub(crate) const MSPI_IOMUX_PIN_NUM_MISO: u32 = 25;
pub(crate) const MSPI_IOMUX_PIN_NUM_MOSI: u32 = 30;
pub(crate) const MSPI_IOMUX_PIN_NUM_CS0: u32 = 24;
pub(crate) const MSPI_IOMUX_PIN_NUM_HD: u32 = 28;
pub(crate) const MSPI_IOMUX_PIN_NUM_WP: u32 = 26;
pub(crate) const SOC_MMU_ENTRY_NUM: u32 = 256;
pub(crate) const SPI_MEM_CS_SETUP_TIME_V: u8 = 0x1F;
pub(crate) const SPI_MEM_CS_HOLD_TIME_V: u8 = 0x1F;

pub(crate) const SOC_DROM_LOW: u32 = 0x4200_0000;
pub(crate) const SOC_DROM_HIGH: u32 = SOC_DROM_LOW + (MMU_PAGE_SIZE * SOC_MMU_ENTRY_NUM);
pub(crate) const SOC_IROM_LOW: u32 = SOC_DROM_LOW;
pub(crate) const SOC_IROM_HIGH: u32 = SOC_DROM_HIGH;
pub(crate) const SOC_RTC_DRAM_LOW: u32 = 0x5000_0000;
pub(crate) const SOC_RTC_DRAM_HIGH: u32 = 0x5000_4000;
pub(crate) const SOC_RTC_IRAM_LOW: u32 = SOC_RTC_DRAM_LOW;
pub(crate) const SOC_RTC_IRAM_HIGH: u32 = SOC_RTC_DRAM_HIGH;

unsafe extern "C" {
    fn Cache_Disable_ICache() -> u32;
    fn Cache_Enable_ICache(autoload: u32);
    fn esp_rom_spiflash_config_clk(freqdiv: u8, spi: u8) -> u32;
    fn esp_rom_spiflash_fix_dummylen(spi: u8, freqdiv: u8);
}

pub(crate) fn hardware_init() {
    unsafe {
        esp_rom_spiflash_config_clk(1, 0);
        esp_rom_spiflash_config_clk(1, 1);
        esp_rom_spiflash_fix_dummylen(0, 1);
        esp_rom_spiflash_fix_dummylen(1, 1);
    }
}

pub(crate) fn cache_disable() {
    unsafe {
        Cache_Disable_ICache();
    }
}

pub(crate) fn cache_enable(l1_autoload_en: bool) {
    const CACHE_LL_L1_ICACHE_AUTOLOAD: u32 = 1;
    let autoload = match l1_autoload_en {
        false => 0,
        true => CACHE_LL_L1_ICACHE_AUTOLOAD,
    };
    unsafe {
        Cache_Enable_ICache(autoload);
    }
}

pub(crate) fn cache_enable_bus() {
    EXTMEM::regs().l1_cache_ctrl().modify(|_, w| {
        w.l1_cache_shut_bus0().clear_bit();
        w.l1_cache_shut_bus1().clear_bit()
    });
}

pub(crate) fn is_l1_autoload_en() -> bool {
    EXTMEM::regs()
        .l1_cache_autoload_ctrl()
        .read()
        .l1_cache_autoload_ena()
        .bit_is_set()
}

pub(crate) fn mmu_set_page_size() {
    const PAGE_SIZE_VAL: u8 = match esp_bootloader_esp_idf::MMU_PAGE_SIZE {
        SIZE_8KB => 3,
        SIZE_16KB => 2,
        SIZE_32KB => 1,
        SIZE_64KB => 0,
        _ => 0,
    };
    SPI0::regs().mmu_power_ctrl().modify(|_, w| unsafe {
        w.spi_mmu_page_size().bits(PAGE_SIZE_VAL)
    });
}

fn mmu_set_entry_raw(index: u32, content: u32) {
    let regs = SPI0::regs();
    regs.mmu_item_index().write(|w| unsafe {
        w.spi_mmu_item_index().bits(index)
    });
    regs.mmu_item_content().write(|w| unsafe {
        w.spi_mmu_item_content().bits(content)
    });
}

pub(crate) fn mmu_set_entry_invalid(index: u32) {
    const SOC_MMU_INVALID: u32 = 0;
    mmu_set_entry_raw(index, SOC_MMU_INVALID);
}

pub(crate) fn mmu_write_entry(index: u32, val: u32) {
    const SOC_MMU_VALID: u32 = 1 << 9;
    mmu_set_entry_raw(index, val | SOC_MMU_VALID);
}
