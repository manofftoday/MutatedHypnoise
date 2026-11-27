/*
 * STM32F103R8T6 - Braids application (MutatedHypnoise).
 *
 * Based on stmlib/linker_scripts/stm32f10x_flash_md_application.ld, with:
 *  - FLASH limited to 108K: 0x08004000 (after the 16K bootloader) up to
 *    0x0801F000, where the 4 x 1K settings pages start (see settings.cc,
 *    Storage<0x8020000, 4>). The linker now fails instead of silently
 *    overlapping the settings.
 *  - Stack at the top of RAM with at least 1.5 KB (see .bss below).
 *  - .ARM.exidx placed in FLASH: recent arm-none-eabi newlib builds
 *    (e.g. memcpy in GCC 14) emit it, and the original script had no
 *    room for it (".ARM.exidx overlaps section .data").
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08004000, LENGTH = 108K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS
{
  .text :
  {
    _start = .;
    KEEP(*(.isr_vector))
    *(.text*)
    *(.rodata*)
    *(.flashtext*)
    KEEP (*(.init))
    KEEP (*(.fini))
  } > FLASH

  .ARM.exidx :
  {
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
  } > FLASH

  _etext = .;
  _sidata = .;

  .data : AT (_sidata)
  {
    _sdata = .;
    *(.ramtext*)
    . = ALIGN(4);
    *(.data*)
    _edata = .;
  } > RAM

  /* Make sure .data's load image also fits before the settings pages. */
  ASSERT(_sidata + SIZEOF(.data) <= ORIGIN(FLASH) + LENGTH(FLASH),
         "Firmware overlaps the settings storage pages")

  .bss :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > RAM

  /* Stack at the top of RAM, growing down towards .bss. The original
   * script reserved only 512 bytes at the *bottom* of RAM, so an overflow
   * ran below 0x20000000 (HardFault) once .data became tiny (MAYA). */
  _estack = ORIGIN(RAM) + LENGTH(RAM);
  _sstack = _estack - 1536;  /* ~3x the measured worst case (~520 B) */
  ASSERT(_ebss <= _sstack, "Less than 1.5 KB of RAM left for the stack")
}
