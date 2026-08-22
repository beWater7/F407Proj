#include "bsp_clkconfig.h"

/* ����SystemInitԴ���޸ĵ� ʱ�����ô��� */
/* 1��ɾ������f407ϵ���޹صĴ���         */

void User_WSetSysClock(void)
{
 RCC_DeInit(); //��λRCC�����мĴ���
/******************************************************************************/
/*            PLL (clocked by HSE) used as System clock source                */
/******************************************************************************/
  __IO uint32_t StartUpCounter = 0, HSEStatus = 0;
  
  /* Enable HSE --- ʹ��HSE */
  RCC->CR |= ((uint32_t)RCC_CR_HSEON);
 
  /* Wait till HSE is ready and if Time out is reached exit */
	/* �ȴ�HSE�����ȶ��������ʱ���˳� */
  do
  {
    HSEStatus = RCC->CR & RCC_CR_HSERDY;
    StartUpCounter++;
  } while((HSEStatus == 0) && (StartUpCounter != HSE_STARTUP_TIMEOUT));

  if ((RCC->CR & RCC_CR_HSERDY) != RESET)  //1: ����  0=reset:δ����
  {
    HSEStatus = (uint32_t)0x01;            //HSE����,״̬λ��1
  }
  else
  {
    HSEStatus = (uint32_t)0x00; 
  }
  
	/* HSE�����ɹ� */
  if (HSEStatus == (uint32_t)0x01)
  {
    /* Select regulator voltage output Scale 1 mode */
		/* ���õ�ѹ��������ģʽΪ1 */
		
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;

    /* HCLK = SYSCLK / 1*/
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;   //AHB 1��Ƶ

  
    /* PCLK2 = HCLK / 2*/
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV2;  //APB2 2��
    
    /* PCLK1 = HCLK / 4*/
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV4;  //APB1 4��

    /* Configure the main PLL */
		/* ���������໷ʱ�� PLL_M: 25��Ƶ  PLL_N:336��Ƶ  PLL_P:2��Ƶ PLL_Q:7  RCC__   :ѡ��HSE */
    /*RCC->PLLCFGR = PLL_M | (PLL_N << 6) | (((PLL_P >> 1) -1) << 16) |
                   (RCC_PLLCFGR_PLLSRC_HSE) | (PLL_Q << 24); */

    RCC->PLLCFGR = 25 | (336 << 6) | (((2 >> 1) -1) << 16) |
                   (RCC_PLLCFGR_PLLSRC_HSE) | (7 << 24);
    /* Enable the main PLL */
		/* ʹ�� PLL      */
    RCC->CR |= RCC_CR_PLLON;

    /* Wait till the main PLL is ready */
		/* �ȴ���PLL�ȶ�  */
    while((RCC->CR & RCC_CR_PLLRDY) == 0)
    {
    }
   
    /* Configure Flash prefetch, Instruction cache, Data cache and wait state */
		/* ����FLASHԤȡֵ��ָ��棬���ݻ��棬�ȴ����� */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;

    /* Select the main PLL as system clock source */
		/* ����PLLΪϵͳʱ��Դ */
    RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_SW));
    RCC->CFGR |= RCC_CFGR_SW_PLL;

    /* Wait till the main PLL is used as system clock source */
		/* ȷ��PLL��ѡΪϵͳʱ�� ����Ӧλ����Ϊ10�� */
    while ((RCC->CFGR & (uint32_t)RCC_CFGR_SWS ) != RCC_CFGR_SWS_PLL)
    {
    }
  }
  else
  { /* If HSE fails to start-up, the application will have wrong clock
         configuration. User can add here some code to deal with this error */
		/* HSE ����ʧ�ܣ��˴���������ʧ�ܵĴ������ */
  }
}


/*  ���ڹ̼���ʵ�ֵ� PLLCLK��Ϊϵͳʱ������ HSE��Ϊ���໷ʱ����Դ */
void HSE_SetSysClock(uint32_t PLLM, uint32_t PLLN, uint32_t PLLP, uint32_t PLLQ)
{
	ErrorStatus HSE_ErrorStatus = ERROR;  //ERROR:0
  //FlagStatus PLL_Status = SET;
	
 /*��λRCC�����мĴ��� */
	RCC_DeInit(); 

  /* Enable HSE --- ʹ��HSE */
  RCC_HSEConfig(RCC_HSE_ON);  //rcc.h�пɲ��Ҷ�Ӧ�ĺ���(һ�������·�)
 
	HSE_ErrorStatus = RCC_WaitForHSEStartUp();
	if(HSE_ErrorStatus == SUCCESS)
	{ 
		/* ��ѹ��������ģʽΪ1 */
	  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;
		
		RCC_HCLKConfig(RCC_SYSCLK_Div1);
		RCC_PCLK1Config(RCC_HCLK_Div4);
    RCC_PCLK2Config(RCC_HCLK_Div2);
		
		RCC_PLLConfig(RCC_PLLSource_HSE, PLLM, PLLN, PLLP, PLLQ);
		
		RCC_PLLCmd(ENABLE);
		
    /* ע��Ѻ�����while����ѭ���ж������ᱻ���ִ�� */		
	  /* ֻ�ź��������û�д�Ч��                      */ 
		while(RESET ==  RCC_GetFlagStatus(RCC_FLAG_PLLRDY))  //���ɹ���ȴ�
		{
		}
		
		/* ����FLASHԤȡֵ��ָ��棬���ݻ��棬�ȴ����� */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;
		
		/*  ����PLLΪϵͳʱ��                     */
		RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
		
		while(0x08 != RCC_GetSYSCLKSource()){}       //����PLL��ȴ�
		
	} 
  else 
  {
	  /* HSE ����ʧ�ܣ��˴����Ӵ��� */
		
	}		
}


/*  ���ڹ̼���ʵ�ֵ� 	PLLCLK��Ϊϵͳʱ������ HSI��Ϊ���໷ʱ����Դ*/
void HSI_SetSysClock(uint32_t PLLM, uint32_t PLLN, uint32_t PLLP, uint32_t PLLQ)
{
	 volatile uint32_t HSI_ErrorStatus = 0;  //ERROR:0
  //FlagStatus PLL_Status = SET;
	
 /*��λRCC�����мĴ��� */
	RCC_DeInit(); 

  /* Enable HSI --- ʹ��HSI */
  RCC_HSICmd(ENABLE);  //rcc.h�пɲ��Ҷ�Ӧ�ĺ���(һ�������·�)
  
	/*�ȴ�HSI������û�ж�Ӧ�ĺ������Լ�ȥ��WaitForHSEStartUp�Ķ��� */
	HSI_ErrorStatus = RCC->CR & RCC_CR_HSIRDY;
	
	/* �ж�HSI�Ƿ������ ��ȡ�Ĵ���RCC->CR�еĵ����ڶ�λ 
	   HSIRDY: 0x0000  0002
	 ������ʱ�� RCC->CR Ӧ��Ϊ0x0000 0003��11��  & 0x0000 0002  ---0x0000 0002(RCC_CR_HSIRDY)
	 û�о���ʱ��             0x0000 0001��01��  & 0x0000 0002  ---0x0000 0000(HSI_ErrorStatus)  */
	
	if(HSI_ErrorStatus == RCC_CR_HSIRDY)   
	{ 
		/* ��ѹ��������ģʽΪ1 */
	  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;
		
		RCC_HCLKConfig(RCC_SYSCLK_Div1);
		RCC_PCLK1Config(RCC_HCLK_Div4);
    RCC_PCLK2Config(RCC_HCLK_Div2);
		
		/* ��������ʱ�� */
		RCC_PLLConfig(RCC_PLLSource_HSI, PLLM, PLLN, PLLP, PLLQ);
		
		/* ʹ�� */
		RCC_PLLCmd(ENABLE);
		
    /* ע��Ѻ�����while����ѭ���ж������ᱻ���ִ�� */		
	  /* ֻ�ź��������û�д�Ч��                      */ 
		while(RESET ==  RCC_GetFlagStatus(RCC_FLAG_PLLRDY))  //���ɹ���ȴ�
		{
		}
		
		/* ����FLASHԤȡֵ��ָ��棬���ݻ��棬�ȴ����� */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;
		
		/*  ����PLLΪϵͳʱ��                     */
		RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
		
		while(0x08 != RCC_GetSYSCLKSource()){}       //����PLL��ȴ�
		
	} 
  else 
  {
	  /* HSI ����ʧ�ܣ��˴����Ӵ��� */
		
	}		
}



/* ��ϵͳʱ��ͨ��MCO1��PA8�������ʹ��ʾ�������Բ鿴ʱ�����*/
void MCO1_GPIO_Config(void)
{
   /* ����GPIO */
	 /* ��һ��: ����GPIO��ʱ��  --> rcc.c --> rcc.h */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	 
	/* �ڶ���: ����GPIO�ĳ�ʼ���ṹ�� */
	GPIO_InitTypeDef GPIO_InitStructure;

	/* ������: ����GPIO��ʼ���ṹ�� */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;     //ʲôʱ�����ùܽ�Ϊ���ù���
  GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;   //����
	
	/* ���Ĳ�: ����GPIO��ʼ���ṹ�壬�����úõĽṹ��ĳ�Ա����д��Ĵ���*/
  GPIO_Init(GPIOA, &GPIO_InitStructure);
	
}

/* ��ϵͳʱ��ͨ��MCO2��PC9����� */
void MCO2_GPIO_Config(void)
{
   /* ����GPIO */
	 /* ��һ��: ����GPIO��ʱ��  --> rcc.c --> rcc.h */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	 
	/* �ڶ���: ����GPIO�ĳ�ʼ���ṹ�� */
	GPIO_InitTypeDef GPIO_InitStructure;

	/* ������: ����GPIO��ʼ���ṹ�� */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;     //ʲôʱ�����ùܽ�Ϊ���ù���
  GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;   //����
	
	/* ���Ĳ�: ����GPIO��ʼ���ṹ�壬�����úõĽṹ��ĳ�Ա����д��Ĵ���*/
  GPIO_Init(GPIOC, &GPIO_InitStructure);
	
}





