.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

.global Reset_Handler
.type Reset_Handler, %function

Reset_Handler:
    /* 设置堆栈指针，通常由链接脚本定义 _estack */
    ldr   r0, =_estack
    mov   sp, r0

    /* 将 .data 段的初始值从 FLASH (_etext) 拷贝到 RAM (_sdata~_edata)
     * 缺少这一步会导致带初值的全局变量在RAM中是随机值 */
    ldr   r0, =_sdata
    ldr   r1, =_edata
    ldr   r2, =_etext
CopyDataLoop:
    cmp   r0, r1
    bcs   CopyDataDone
    ldr   r3, [r2], #4
    str   r3, [r0], #4
    b     CopyDataLoop
CopyDataDone:

    /* 清零 .bss 段 (_sbss~_ebss)
     * 缺少这一步会导致未初始化的全局/静态变量不是0，而是随机值 */
    ldr   r0, =_sbss
    ldr   r1, =_ebss
ZeroBssLoop:
    cmp   r0, r1
    bcs   ZeroBssDone
    movs  r2, #0
    str   r2, [r0], #4
    b     ZeroBssLoop
ZeroBssDone:

    /* 调用系统初始化函数 */
    bl    SystemInit

    /* 调用你自己写的 PSRAM 初始化函数 */
    bl    FSMC_SRAM_Init

    /* 跳转到 main 函数 */
    bl    main

    /* 防止 main 返回，死循环 */
1:  b     1b
