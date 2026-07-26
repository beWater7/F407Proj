ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 32K      /* Bootloader, 与 iap.h 中 BOOT_FLASH_SIZE 保持一致，避免越界覆盖 APP1(0x08008000) */
  RAM   (xrw)     : ORIGIN = 0x20000000, LENGTH = 128K
  PSRAM (xrw)     : ORIGIN = 0x6C000000, LENGTH = 1020K
}

/* 定义栈顶地址符号（链接器使用） */
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .text :
  {
    KEEP(*(.isr_vector))           /* Startup code */

    *(.text*)                      /* Program code */
    *(.rodata*)                    /* Read-only data */

    /* 加入 .ARM.exidx 段 */
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
	_end = .;    /*加上这句 */
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

  /* Heap and stack symbols */
  _heap_start = ORIGIN(PSRAM);
  _heap_end   = ORIGIN(PSRAM) + LENGTH(PSRAM);

  _stack_top = _estack;

  /* Symbols for startup code */
  PROVIDE(__etext = _etext);
  PROVIDE(__data_start__ = _sdata);
  PROVIDE(__data_end__ = _edata);
  PROVIDE(__bss_start__ = _sbss);
  PROVIDE(__bss_end__ = _ebss);
}