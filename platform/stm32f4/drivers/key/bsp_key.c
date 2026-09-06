#include "bsp_key.h"


/**
  * @brief  配置嵌套向量中断控制器NVIC
  * @param  无
  * @retval 无
  */
static void NVIC_Configuration(void)
{
  NVIC_InitTypeDef NVIC_InitStructure;
  
  /* 配置NVIC为优先级组1 */
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
  
  /* 配置中断源：按键1 */
  NVIC_InitStructure.NVIC_IRQChannel = KEY1_INT_EXTI_IRQ;
  /* 配置抢占优先级：1 */
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  /* 配置子优先级：1 */
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  /* 使能中断通道 */
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
  
  /* 配置中断源：按键2，其他使用上面相关配置 */  
  NVIC_InitStructure.NVIC_IRQChannel = KEY2_INT_EXTI_IRQ;
  NVIC_Init(&NVIC_InitStructure);
}



void EXTI_Key_Config(void)
{
		GPIO_InitTypeDef GPIO_InitStructure; 
	  EXTI_InitTypeDef EXTI_InitStructure;
	
     /* 第一步 初始化需要连接到EXTI输入线的GPIO */
	   
	
	   /*开启按键GPIO口的时钟 PA0 和 PC13 */
	   RCC_AHB1PeriphClockCmd(KEY1_INT_GPIO_CLK|KEY2_INT_GPIO_CLK ,ENABLE);
	   
	
	   /* 选择按键1的引脚 */ 
  GPIO_InitStructure.GPIO_Pin = KEY1_INT_GPIO_PIN;
  /* 设置引脚为输入模式 */ 
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;	    		
  /* 设置引脚不上拉也不下拉 */
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  /* 使用上面的结构体初始化按键 */
  GPIO_Init(KEY1_INT_GPIO_PORT, &GPIO_InitStructure); 
	
	/* 选择按键2的引脚 */ 
  GPIO_InitStructure.GPIO_Pin = KEY2_INT_GPIO_PIN;  
  /* 其他配置与上面相同 */
  GPIO_Init(KEY2_INT_GPIO_PORT, &GPIO_InitStructure);      
	
 
  /* 第二步 初始化EXTI */
	
	/* 使能 SYSCFG 时钟 ，使用GPIO外部中断时必须使能SYSCFG时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
	
	/* 连接 EXTI 中断源 到key1引脚 */
  SYSCFG_EXTILineConfig(KEY1_INT_EXTI_PORTSOURCE,KEY1_INT_EXTI_PINSOURCE);
	
		/* 连接 EXTI 中断源 到key2 引脚 */
  SYSCFG_EXTILineConfig(KEY2_INT_EXTI_PORTSOURCE,KEY2_INT_EXTI_PINSOURCE);
	
	
	 /* 选择 EXTI 中断源 */
  EXTI_InitStructure.EXTI_Line = KEY1_INT_EXTI_LINE;
  /* 中断模式 */
  EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
  /* 上升沿触发 */
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;  
  /* 使能中断/事件线 */
  EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  EXTI_Init(&EXTI_InitStructure);
	
	
	 /* 选择 EXTI 中断源 */
  EXTI_InitStructure.EXTI_Line = KEY2_INT_EXTI_LINE;
  EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
  /* 下降沿触发 */
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;  
  EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  EXTI_Init(&EXTI_InitStructure);
	
	
	   /* 第三步 配置NVIC */
	 NVIC_Configuration();
		 
	 
	   /* 第四步 编写中断服务函数 */

}
/**
  * @brief  配置按键用到的I/O口
  * @param  无
  * @retval 无
  */
void Key_GPIO_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    /*开启按键GPIO口的时钟*/
    RCC_AHB1PeriphClockCmd(KEY1_GPIO_CLK|KEY2_GPIO_CLK,ENABLE);

    /*选择按键的引脚*/
    GPIO_InitStructure.GPIO_Pin = KEY1_GPIO_PIN; 

    /*设置引脚为输入模式*/
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN; 

    /*设置引脚不上拉也不下拉*/
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;

    /*使用上面的结构体初始化按键*/
    GPIO_Init(KEY1_GPIO_PORT, &GPIO_InitStructure);  


    /*选择按键的引脚*/
    GPIO_InitStructure.GPIO_Pin = KEY2_GPIO_PIN; 

    /*使用上面的结构体初始化按键*/
    GPIO_Init(KEY2_GPIO_PORT, &GPIO_InitStructure);

}

/**
  * @brief   检测是否有按键按下
  * @param   具体的端口和端口位
  *		@arg GPIOx: x可以是（A...G） 
  *		@arg GPIO_PIN 可以是GPIO_PIN_x（x可以是1...16）
  * @retval  按键的状态
  *		@arg KEY_ON:按键按下
  *		@arg KEY_OFF:按键没按下
  */
uint8_t Key_Scan(GPIO_TypeDef* GPIOx,uint16_t GPIO_Pin)
{			
	/*检测是否有按键按下 */
	if(GPIO_ReadInputDataBit(GPIOx,GPIO_Pin) == KEY_ON )  
	{	 
		/*等待按键释放 */
		while(GPIO_ReadInputDataBit(GPIOx,GPIO_Pin) == KEY_ON);   
		return 	KEY_ON;	 
	}
	else
		return KEY_OFF;
}
