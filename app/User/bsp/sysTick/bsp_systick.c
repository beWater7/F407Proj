#include "bsp_systick.h"
#include "FreeRTOSConfig.h"


__IO u32 TimingDelay;

/* 定时器的初始化 */
void SysTick_Init(void)
{
 /* SystemFrequency / 1000    1ms中断一次
	 * SystemFrequency / 100000	 10us中断一次
	 * SystemFrequency / 1000000 1us中断一次
	 */
	//if (SysTick_Config(SystemCoreClock / 1000))  //1ms一次
	/* 使用FreeRTOS配置的系统时钟, 目前FreeRTOS使用的时钟就是系统时钟, 可修改 */
	if(SysTick_Config(configCPU_CLOCK_HZ / 1000))
	{ 
		/* Capture error */ 
		while (1);
	}
    //printf("[%s:%d] systick init!\n",__FUNCTION__,__LINE__);
}



/**
  * @brief   ms延时程序
  * @param  
  *		@arg nTime: Delay_us( 1 ) 则实现的延时为 1 * 10us = 10us
  * @retval  无
  */
void Delay_ms(__IO u32 nTime)
{ 
	TimingDelay = nTime;	

	while(TimingDelay != 0);
}

/**
  * @brief   us延时程序,1ms为一个单位
  * @param  
  *		@arg nTime: Delay_us( 1 ) 则实现的延时为 1 * 10us = 10us
  * @retval  无
  */
void Delay_10ms(__IO u32 nTime)
{ 
	TimingDelay = nTime;	

	while(TimingDelay != 0);
}

/**
  * @brief  获取节拍程序
  * @param  无
  * @retval 无
  * @attention  在 SysTick 中断函数 SysTick_Handler()调用
  */
void TimingDelay_Decrement(void)
{
	if (TimingDelay != 0x00)
	{ 
		TimingDelay--;
	}
}

/* 不依赖中断的一种定时  */
void SysTick_Delay_Ms(__IO uint32_t ms)
{
   uint32_t i;
	 SysTick_Config(SystemCoreClock / 1000);
	 
	for(i=0; i< ms; i++)
	{
	   //当计数器的值减小到0时，CRTL寄存器的第16位会置1
		 //当置1时，读取该位会清0
		 while(!((SysTick->CTRL) & (1 << 16))); //等待CTRL置1，需要1ms  1ms* （ms） = （ms） ms
		
	}
	
	//关闭SysTick定时器
  SysTick->CTRL &=~SysTick_CTRL_ENABLE_Msk;	
}
