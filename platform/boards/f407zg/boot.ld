/*
 * stage0/link.ld — 固化 boot（stage0）
 *
 * 只做一件事：多介质加载 loader 到 SRAM 并跳转。自身在内部 Flash XIP，
 * .data/.bss 放 SRAM 高端 0x2001C000~0x20020000，栈从 0x20020000 向下，
 * 与 loader 运行区（0x20000000~0x20010000）及堆（0x20010000~0x2001C000）不冲突。
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)   : ORIGIN = 0x08000000, LENGTH = 32K      /* 与 loader_meta.h / iap.h BOOT_FLASH_SIZE 一致 */
  RAM   (xrw)  : ORIGIN = 0x2001C000, LENGTH = 16K      /* 高端 SRAM，避开 loader 区 */
}

_estack = 0x20020000;   /* 128K SRAM 顶部 */

SECTIONS
{
  .isr_vector :
  {
    KEEP(*(.isr_vector))           /* Startup code */
  } > FLASH

  .text :
  {
    *(.text*)                      /* Program code */
    *(.rodata*)                    /* Read-only data */

    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .data : AT(_etext)
  {
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM

  .bss :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    _end = .;
  } > RAM

  /* 堆固定到 loader 区与 stage0 数据区之间的空闲 SRAM */
  _heap_start = 0x20010000;
  _heap_end   = 0x2001C000;

  _stack_top = _estack;

  PROVIDE(__etext = _etext);
  PROVIDE(__data_start__ = _sdata);
  PROVIDE(__data_end__ = _edata);
  PROVIDE(__bss_start__ = _sbss);
  PROVIDE(__bss_end__ = _ebss);
}
