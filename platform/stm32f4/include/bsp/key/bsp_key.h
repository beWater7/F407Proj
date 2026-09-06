#ifndef _EXTI_H_
#define _EXTI_H_

#include "stm32f4xx.h"


#define KEY1_INT_GPIO_PORT                GPIOA
#define KEY1_INT_GPIO_CLK                 RCC_AHB1Periph_GPIOA
#define KEY1_INT_GPIO_PIN                 GPIO_Pin_0
#define KEY1_INT_EXTI_PORTSOURCE          EXTI_PortSourceGPIOA
#define KEY1_INT_EXTI_PINSOURCE           EXTI_PinSource0
#define KEY1_INT_EXTI_LINE                EXTI_Line0 /* PA-PK0 都使用line0 */
#define KEY1_INT_EXTI_IRQ                 EXTI0_IRQn /* 引脚0适应EXTI0 */

#define KEY1_IRQHandler                   EXTI0_IRQHandler

#define KEY2_INT_GPIO_PORT                GPIOC
#define KEY2_INT_GPIO_CLK                 RCC_AHB1Periph_GPIOC
#define KEY2_INT_GPIO_PIN                 GPIO_Pin_13
#define KEY2_INT_EXTI_PORTSOURCE          EXTI_PortSourceGPIOC
#define KEY2_INT_EXTI_PINSOURCE           EXTI_PinSource13
#define KEY2_INT_EXTI_LINE                EXTI_Line13
#define KEY2_INT_EXTI_IRQ                 EXTI15_10_IRQn /* 只能使用启动文件规定的中断源，15-10指的是10-15？引脚13介于范围内 */

#define KEY2_IRQHandler                   EXTI15_10_IRQHandler

#define KEY1_GPIO_PORT  GPIOA
#define KEY1_GPIO_CLK   RCC_AHB1Periph_GPIOA
#define KEY1_GPIO_PIN   GPIO_Pin_0

#define KEY2_GPIO_PORT  GPIOC
#define KEY2_GPIO_CLK   RCC_AHB1Periph_GPIOC
#define KEY2_GPIO_PIN   GPIO_Pin_13


 /** 按键按下标置宏
	* 按键按下为高电平，设置 KEY_ON=1， KEY_OFF=0
	* 若按键按下为低电平，把宏设置成KEY_ON=0 ，KEY_OFF=1 即可
	*/
#define KEY_ON	1
#define KEY_OFF	0

void EXTI_Key_Config(void);
void Key_GPIO_Config(void);
uint8_t Key_Scan(GPIO_TypeDef* GPIOx,uint16_t GPIO_Pin);
#endif

