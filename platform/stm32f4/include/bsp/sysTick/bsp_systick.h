#ifndef __SYSTICK_H
#define __SYSTICK_H

#include "stm32f4xx.h"


void SysTick_Init(void);
void Delay_ms(__IO u32 nTime);
void SysTick_Delay_Ms(__IO uint32_t ms);
void Delay_10ms(__IO u32 nTime);
void TimingDelay_Decrement(void);
#endif

