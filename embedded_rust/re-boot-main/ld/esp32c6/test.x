/** Simplified memory map for the bootloader.
 *  Make sure the bootloader can load into main memory without overwriting itself.
 *
 *  ESP32-C6 ROM static data usage is as follows:
 *  - 0x4086ad08 - 0x4087c610: Shared buffers, used in UART/USB/SPI download mode only
 *  - 0x4087c610 - 0x4087e610: PRO CPU stack, can be reclaimed as heap after RTOS startup
 *  - 0x4087e610 - 0x40880000: ROM .bss and .data (not easily reclaimable)
 *
 *  The 2nd stage bootloader can take space up to the end of ROM shared
 *  buffers area (0x4087c610).
 */

/* We consider 0x4087c610 to be the last usable address for 2nd stage bootloader stack overhead, dram_seg,
 * and work out iram_seg and iram_loader_seg addresses from there, backwards.
 */

/* These lengths can be adjusted, if necessary: */
bootloader_dram_seg_len = 0x5000;
bootloader_iram_loader_seg_len = 0xA000;

bootloader_dram_seg_start = bootloader_iram_loader_seg_start + bootloader_iram_loader_seg_len;
bootloader_iram_loader_seg_start = 0x40800000;

MEMORY
{
  ROM : org = bootloader_iram_loader_seg_start, len = bootloader_iram_loader_seg_len
  RAM : org = bootloader_dram_seg_start, len = bootloader_dram_seg_len
  APP_ROM : org = 0x42000000 , len = 0x1000
}

SECTIONS {
  .rodata_app : ALIGN(4)
  {
    KEEP(*(.rodata_app));
  } > APP_ROM

  .text_gap_app (NOLOAD): {
    . = . + 4;
    . = ALIGN(4) + 0x20;
  } > APP_ROM

  .text_app : ALIGN(4)
  {
    KEEP(*(.text_app));
  } > APP_ROM
}

REGION_ALIAS("ROTEXT", ROM);
REGION_ALIAS("RODATA", ROM);

REGION_ALIAS("RWTEXT", RAM);
REGION_ALIAS("RWDATA", RAM);

REGION_ALIAS("RTC_FAST_RWTEXT", RAM);
REGION_ALIAS("RTC_FAST_RWDATA", RAM);

INCLUDE "esp32c6.x"
INCLUDE "hal-defaults.x"