#include "bsp_clkconfig.h"

/* 基于SystemInit源码修改的 时钟配置代码 */
/* 1、删除了与f407系列无关的代码         */

void User_WSetSysClock(void)
{
 RCC_DeInit(); //复位RCC的所有寄存器
/******************************************************************************/
/*            PLL (clocked by HSE) used as System clock source                */
/******************************************************************************/
  __IO uint32_t StartUpCounter = 0, HSEStatus = 0;
  
  /* Enable HSE --- 使能HSE */
  RCC->CR |= ((uint32_t)RCC_CR_HSEON);
 
  /* Wait till HSE is ready and if Time out is reached exit */
	/* 等待HSE启动稳定，如果超时则退出 */
  do
  {
    HSEStatus = RCC->CR & RCC_CR_HSERDY;
    StartUpCounter++;
  } while((HSEStatus == 0) && (StartUpCounter != HSE_STARTUP_TIMEOUT));

  if ((RCC->CR & RCC_CR_HSERDY) != RESET)  //1: 就绪  0=reset:未就绪
  {
    HSEStatus = (uint32_t)0x01;            //HSE就绪,状态位置1
  }
  else
  {
    HSEStatus = (uint32_t)0x00; 
  }
  
	/* HSE启动成功 */
  if (HSEStatus == (uint32_t)0x01)
  {
    /* Select regulator voltage output Scale 1 mode */
		/* 设置电压调节器的模式为1 */
		
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;

    /* HCLK = SYSCLK / 1*/
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;   //AHB 1分频

  
    /* PCLK2 = HCLK / 2*/
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV2;  //APB2 2分
    
    /* PCLK1 = HCLK / 4*/
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV4;  //APB1 4分

    /* Configure the main PLL */
		/* 配置主锁相环时钟 PLL_M: 25分频  PLL_N:336倍频  PLL_P:2分频 PLL_Q:7  RCC__   :选择HSE */
    /*RCC->PLLCFGR = PLL_M | (PLL_N << 6) | (((PLL_P >> 1) -1) << 16) |
                   (RCC_PLLCFGR_PLLSRC_HSE) | (PLL_Q << 24); */

    RCC->PLLCFGR = 25 | (336 << 6) | (((2 >> 1) -1) << 16) |
                   (RCC_PLLCFGR_PLLSRC_HSE) | (7 << 24);
    /* Enable the main PLL */
		/* 使能 PLL      */
    RCC->CR |= RCC_CR_PLLON;

    /* Wait till the main PLL is ready */
		/* 等待主PLL稳定  */
    while((RCC->CR & RCC_CR_PLLRDY) == 0)
    {
    }
   
    /* Configure Flash prefetch, Instruction cache, Data cache and wait state */
		/* 配置FLASH预取值，指令缓存，数据缓存，等待周期 */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;

    /* Select the main PLL as system clock source */
		/* 设置PLL为系统时钟源 */
    RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_SW));
    RCC->CFGR |= RCC_CFGR_SW_PLL;

    /* Wait till the main PLL is used as system clock source */
		/* 确保PLL被选为系统时钟 （对应位被置为10） */
    while ((RCC->CFGR & (uint32_t)RCC_CFGR_SWS ) != RCC_CFGR_SWS_PLL);
    {
    }
  }
  else
  { /* If HSE fails to start-up, the application will have wrong clock
         configuration. User can add here some code to deal with this error */
		/* HSE 启动失败，此处添加启动失败的错误代码 */
  }
}


/*  基于固件库实现的 PLLCLK作为系统时钟配置 HSE作为锁相环时钟来源 */
void HSE_SetSysClock(uint32_t PLLM, uint32_t PLLN, uint32_t PLLP, uint32_t PLLQ)
{
	ErrorStatus HSE_ErrorStatus = ERROR;  //ERROR:0
  //FlagStatus PLL_Status = SET;
	
 /*复位RCC的所有寄存器 */
	RCC_DeInit(); 

  /* Enable HSE --- 使能HSE */
  RCC_HSEConfig(RCC_HSE_ON);  //rcc.h中可查找对应的函数(一般在最下方)
 
	HSE_ErrorStatus = RCC_WaitForHSEStartUp();
	if(HSE_ErrorStatus == SUCCESS)
	{ 
		/* 电压调节器的模式为1 */
	  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;
		
		RCC_HCLKConfig(RCC_SYSCLK_Div1);
		RCC_PCLK1Config(RCC_HCLK_Div4);
    RCC_PCLK2Config(RCC_HCLK_Div2);
		
		RCC_PLLConfig(RCC_PLLSource_HSE, PLLM, PLLN, PLLP, PLLQ);
		
		RCC_PLLCmd(ENABLE);
		
    /* 注意把函数放while里作循换判断条件会被多次执行 */		
	  /* 只放函数结果则没有此效果                      */ 
		while(RESET ==  RCC_GetFlagStatus(RCC_FLAG_PLLRDY))  //不成功则等待
		{
		}
		
		/* 配置FLASH预取值，指令缓存，数据缓存，等待周期 */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;
		
		/*  设置PLL为系统时钟                     */
		RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
		
		while(0x08 != RCC_GetSYSCLKSource()){}       //不是PLL则等待
		
	} 
  else 
  {
	  /* HSE 启动失败，此处添加代码 */
		
	}		
}


/*  基于固件库实现的 	PLLCLK作为系统时钟配置 HSI作为锁相环时钟来源*/
void HSI_SetSysClock(uint32_t PLLM, uint32_t PLLN, uint32_t PLLP, uint32_t PLLQ)
{
	 volatile uint32_t HSI_ErrorStatus = 0;  //ERROR:0
  //FlagStatus PLL_Status = SET;
	
 /*复位RCC的所有寄存器 */
	RCC_DeInit(); 

  /* Enable HSI --- 使能HSI */
  RCC_HSICmd(ENABLE);  //rcc.h中可查找对应的函数(一般在最下方)
  
	/*等待HSI启动，没有对应的函数，自己去看WaitForHSEStartUp的定义 */
	HSI_ErrorStatus = RCC->CR & RCC_CR_HSIRDY;
	
	/* 判断HSI是否就绪， 读取寄存气RCC->CR中的倒数第二位 
	   HSIRDY: 0x0000  0002
	 当就绪时， RCC->CR 应该为0x0000 0003（11）  & 0x0000 0002  ---0x0000 0002(RCC_CR_HSIRDY)
	 没有就绪时，             0x0000 0001（01）  & 0x0000 0002  ---0x0000 0000(HSI_ErrorStatus)  */
	
	if(HSI_ErrorStatus == RCC_CR_HSIRDY)   
	{ 
		/* 电压调节器的模式为1 */
	  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;
		
		RCC_HCLKConfig(RCC_SYSCLK_Div1);
		RCC_PCLK1Config(RCC_HCLK_Div4);
    RCC_PCLK2Config(RCC_HCLK_Div2);
		
		/* 配置锁向环时钟 */
		RCC_PLLConfig(RCC_PLLSource_HSI, PLLM, PLLN, PLLP, PLLQ);
		
		/* 使能 */
		RCC_PLLCmd(ENABLE);
		
    /* 注意把函数放while里作循换判断条件会被多次执行 */		
	  /* 只放函数结果则没有此效果                      */ 
		while(RESET ==  RCC_GetFlagStatus(RCC_FLAG_PLLRDY))  //不成功则等待
		{
		}
		
		/* 配置FLASH预取值，指令缓存，数据缓存，等待周期 */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;
		
		/*  设置PLL为系统时钟                     */
		RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
		
		while(0x08 != RCC_GetSYSCLKSource()){}       //不是PLL则等待
		
	} 
  else 
  {
	  /* HSI 启动失败，此处添加代码 */
		
	}		
}



/* 将系统时钟通过MCO1（PA8）输出，使用示波器可以查看时钟输出*/
void MCO1_GPIO_Config(void)
{
   /* 配置GPIO */
	 /* 第一步: 开启GPIO的时钟  --> rcc.c --> rcc.h */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	 
	/* 第二步: 定义GPIO的初始化结构体 */
	GPIO_InitTypeDef GPIO_InitStructure;

	/* 第三步: 配置GPIO初始化结构体 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;     //什么时候设置管脚为复用功能
  GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;   //上拉
	
	/* 第四步: 调用GPIO初始化结构体，把配置好的结构体的成员参数写入寄存器*/
  GPIO_Init(GPIOA, &GPIO_InitStructure);
	
}

/* 将系统时钟通过MCO2（PC9）输出 */
void MCO2_GPIO_Config(void)
{
   /* 配置GPIO */
	 /* 第一步: 开启GPIO的时钟  --> rcc.c --> rcc.h */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	 
	/* 第二步: 定义GPIO的初始化结构体 */
	GPIO_InitTypeDef GPIO_InitStructure;

	/* 第三步: 配置GPIO初始化结构体 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;     //什么时候设置管脚为复用功能
  GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;   //上拉
	
	/* 第四步: 调用GPIO初始化结构体，把配置好的结构体的成员参数写入寄存器*/
  GPIO_Init(GPIOC, &GPIO_InitStructure);
	
}





