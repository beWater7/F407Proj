#ifndef __BSP_TIMER_H__
#define __BSP_TIMER_H__


void TIM3_init(void);

void TIM3_DeInit(void);

/* 中断服务函数，需要全局可见以覆盖启动文件里的弱符号 */
void TIM3_IRQHandler(void);

void TIM3_sleep_10ms(unsigned int mTime);

#endif /* __BSP_TIMER_H__ */