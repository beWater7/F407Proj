//bsp: board support package 板级支持包

#include "bsp_led.h"


void myDelay(int x)
{ 
   for(; x!=0; x--);

}

void LED_GPIO_Config(void)
{
#if 0	
 /* 第一步: 开启GPIO的时钟  --> rcc.c --> rcc.h */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);
	 
	
	/* 第二步: 定义GPIO的初始化结构体 */
	GPIO_InitTypeDef GPIO_InitStructure;

	/* 第三步: 配置GPIO初始化结构体 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;   //上拉电阻, 步设置上拉电阻，灯不会完全灭，而是绿色
	
	/* 第四步: 调用GPIO初始化结构体，把配置好的结构体的成员参数写入寄存器*/
  GPIO_Init(GPIOF, &GPIO_InitStructure);
	
	//设置Pin_6低电平
	GPIO_SetBits(GPIOF, GPIO_Pin_6);
#endif
    /*开启LED相关的GPIO外设时钟*/
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd ( LED1_GPIO_CLK|
                           LED2_GPIO_CLK|
                           LED3_GPIO_CLK, ENABLE); 

    /*选择要控制的GPIO引脚*/
    GPIO_InitStructure.GPIO_Pin = LED1_PIN;	

    /*设置引脚模式为输出模式*/
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;

    /*设置引脚的输出类型为推挽输出*/
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;

    /*设置引脚为上拉模式*/
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;

    /*设置引脚速率为2MHz */   
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz; 

    /*调用库函数，使用上面配置的GPIO_InitStructure初始化GPIO*/
    GPIO_Init(LED1_GPIO_PORT, &GPIO_InitStructure);	

    /*选择要控制的GPIO引脚*/
    GPIO_InitStructure.GPIO_Pin = LED2_PIN;	
    GPIO_Init(LED2_GPIO_PORT, &GPIO_InitStructure);	

    /*选择要控制的GPIO引脚*/
    GPIO_InitStructure.GPIO_Pin = LED3_PIN;	
    GPIO_Init(LED3_GPIO_PORT, &GPIO_InitStructure);	

    /*关闭RGB灯*/
    LED_RGBOFF;

}

void SHAN(void)
{
    while(1){
	   
		 GPIO_ResetBits(GPIOF, GPIO_Pin_6); 
	   myDelay(0xfffff);
     GPIO_SetBits(GPIOF, GPIO_Pin_6);	
     myDelay(0xfffff);
		 
	 }

}


