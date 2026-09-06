/*
 * link_ram.ld — loader 的 SRAM 运行变体
 *
 * 由 boot(stage0) 从 SPI Flash / 内部 Flash / UART 搬运到 0x20000000 后执行。
 * 全部段（isr_vector/text/rodata/data/bss）都放在 RAM 低 64K，
 * heap 用外部 PSRAM（沿用 syscalls.c 的 _sbrk）。
 *
 * 注意：
 *  - .data 不写 AT()，LMA = VMA，reset_handler 的 .data 自拷贝是幂等无害操作
 *  - 栈顶取整片 128K SRAM 顶部 0x20020000；boot 的数据区（0x2001C000 高端）
 *    在跳转后即失效，不与 loader 冲突
 */
ENTRY(Reset_Handler)

MEMORY
{
  RAM   (xrw)     : ORIGIN = 0x20000000, LENGTH = 64K      /* loader 运行区，低 64K */
  PSRAM (xrw)     : ORIGIN = 0x6C000000, LENGTH = 1020K
}

_estack = 0x20020000;   /* 128K SRAM 顶部 */

SECTIONS
{
  .isr_vector :
  {
    __vector_base = .;
    KEEP(*(.isr_vector))           /* Startup code */
  } > RAM

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
  } > RAM

  .data :
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

  .heap (NOLOAD) :
  {
    *(.heap)
    . = ALIGN(4);
  } > PSRAM

  .exram (NOLOAD) :
  {
    *(.EXRAM)
    . = ALIGN(4);
  } > PSRAM

  _heap_start = ORIGIN(PSRAM);
  _heap_end   = ORIGIN(PSRAM) + LENGTH(PSRAM);

  _stack_top = _estack;

  PROVIDE(__etext = _etext);
  PROVIDE(__data_start__ = _sdata);
  PROVIDE(__data_end__ = _edata);
  PROVIDE(__bss_start__ = _sbss);
  PROVIDE(__bss_end__ = _ebss);
}
