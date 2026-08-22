ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08008000, LENGTH = 256K     /* APP1 (alias of link_app1.ld) */
  RAM   (xrw)     : ORIGIN = 0x20000000, LENGTH = 128K
  PSRAM (xrw)     : ORIGIN = 0x6C000000, LENGTH = 1020K
}

/* 栈顶：用 RAM 末尾-4，避免 0x20020000 被旧版 jump_app 的 ST 掩码误判为非法 MSP */
_estack = ORIGIN(RAM) + LENGTH(RAM) - 4;

SECTIONS
{
  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)

    /*
     * INIT_EXPORT / POLL_EXPORT（export.h）
     * 必须连续排布，供 xplat_run() 扫描
     */
    . = ALIGN(4);
    __xplat_export_start = .;
    KEEP(*(SORT_BY_NAME(xplat_export)))
    KEEP(*(SORT_BY_NAME(xplat_export.*)))
    KEEP(*(SORT_BY_NAME(.xplat_export*)))
    __xplat_export_end = .;

    . = ALIGN(4);
    __expoll_start = .;
    KEEP(*(SORT_BY_NAME(expoll)))
    KEEP(*(SORT_BY_NAME(expoll.*)))
    KEEP(*(SORT_BY_NAME(.expoll*)))
    __expoll_end = .;

    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;
  } > FLASH

  _sidata = LOADADDR(.data);

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

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    *(.heap)
    . = ALIGN(8);
  } > PSRAM

  /*
   * __EXRAM → section("EXRAM")（无前导点）。
   * 旧脚本只写了 *(.EXRAM)，导致 ucHeap 掉进内部 SRAM 且未对齐，
   * FreeRTOS 第一次 xTaskCreate 就可能 HardFault，表现为卡在 board bsp 之后。
   */
  .exram (NOLOAD) :
  {
    . = ALIGN(8);
    *(EXRAM)
    *(.EXRAM)
    *(.exram*)
    . = ALIGN(8);
    _exram_end = .;
  } > PSRAM

  /* newlib _sbrk 若从 PSRAM 起点长，会和 ucHeap（FreeRTOS）重叠：
   * HTTP JSON 盖掉 g_pstDevParam，rebuildCfg 再写进 config，hex_dump 里就会看到 errorMsg/ssid。 */
  _heap_start = _exram_end;
  _heap_end   = ORIGIN(PSRAM) + LENGTH(PSRAM);
  _stack_top = _estack;

  PROVIDE(__etext = _etext);
  PROVIDE(__data_start__ = _sdata);
  PROVIDE(__data_end__ = _edata);
  PROVIDE(__bss_start__ = _sbss);
  PROVIDE(__bss_end__ = _ebss);
}
