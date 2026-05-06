#![no_std]
#![no_main]
#![deny(
    clippy::mem_forget,
    reason = "mem::forget is generally not safe to do with esp_hal types, especially those \
    holding buffers for the duration of a data transfer."
)]
#![deny(clippy::large_stack_frames)]

use esp_hal::clock::CpuClock;
use esp_hal::main;
use esp_storage::FlashStorage;
use log::info;
use re_boot::{find_app_partitions, init, load_app};
use re_boot::mmu_cache::MmuCache;

#[cfg(feature = "test-as-app")]
use esp_backtrace as _;

#[cfg(not(feature = "test-as-app"))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    log::error!("[boot] Panic! at the bootloader:");
    //log::error!("[boot] {info}");
    esp_hal::rom::software_reset();
}

// This creates a default app-descriptor required by the esp-idf bootloader.
// For more information see: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/app_image_format.html#application-description>
esp_bootloader_esp_idf::esp_app_desc!();

#[allow(
    clippy::large_stack_frames,
    reason = "it's not unusual to allocate larger buffers etc. in main"
)]
#[main]
fn main() -> ! {
    // generator version: 1.2.0

    esp_println::logger::init_logger_from_env();

    info!("[boot] 🦀 2nd stage bootloader");
    let config = esp_hal::Config::default().with_cpu_clock(CpuClock::max());
    let p = esp_hal::init(config);
    init();
    let mut mmucache = MmuCache::new(p.SPI0);
    let mut flash = FlashStorage::new(p.FLASH);
    let parts = find_app_partitions(&mut flash).unwrap();
    let entry = load_app(parts, &mut mmucache, &mut flash).unwrap();
    info!("[boot] Starting app...");
    entry();
}
