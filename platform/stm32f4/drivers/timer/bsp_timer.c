#include "stm32f4xx.h"
#if defined(CONFIG_APP_MULTIBUTTON)
#include "multi_button.h"
#endif

__IO u32 TIM3Delay;

/* app/main.c 提供强符号；loader/boot 无此变量时使用本弱定义 */
__attribute__((weak)) volatile uint32_t LocalTime;


/**
  * @brief  获取节拍程序
  * @param  无
  * @retval 无
  * @attention  在 SysTick 中断函数 SysTick_Handler()调用
  */
static void Tim3Delay_Decrement(void)
{
    if (TIM3Delay != 0x00)
    {
        TIM3Delay--;
    }
}


/**
  * @brief  定时器3中断服务函数
  * @param  无
  * @retval 无
  * @note   不能加 static：启动文件向量表里是弱符号 TIM3_IRQHandler，
  *         static 会导致链接器用不了这个函数覆盖弱符号，最终仍指向
  *         Default_Handler 死循环。board_bsp_init 里一开 TIM3 并
  *         __enable_irq 后，程序就会卡死，看起来像 APP 没打印。
  */
void TIM3_IRQHandler(void)
{
#if defined(CONFIG_APP_MULTIBUTTON)
    button_ticks();
#endif
    if(TIM_GetITStatus(TIM3,TIM_IT_Update)==SET) //溢出中断
    {
        LocalTime+=10;//10ms增量
    }
    Tim3Delay_Decrement();
    TIM_ClearITPendingBit(TIM3,TIM_IT_Update);  //清除中断标志位
}


/**
  * @brief  通用定时器3中断初始化
  * @param  period : 自动重装值。
  * @param  prescaler : 时钟预分频数
  * @retval 无
  * @note   定时器溢出时间计算方法:Tout=((period+1)*(prescaler+1))/Ft us.
  *          Ft=定时器工作频率,为SystemCoreClock/2=90,单位:Mhz
  */
void TIM3_Config(uint16_t period,uint16_t prescaler)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3,ENABLE);  ///使能TIM3时钟

    TIM_TimeBaseInitStructure.TIM_Prescaler=prescaler;  //定时器分频
    TIM_TimeBaseInitStructure.TIM_CounterMode=TIM_CounterMode_Up; //向上计数模式
    TIM_TimeBaseInitStructure.TIM_Period=period;   //自动重装载值
    TIM_TimeBaseInitStructure.TIM_ClockDivision=TIM_CKD_DIV1; 

    TIM_TimeBaseInit(TIM3,&TIM_TimeBaseInitStructure);

    TIM_ITConfig(TIM3,TIM_IT_Update,ENABLE); //允许定时器3更新中断
    TIM_Cmd(TIM3,ENABLE); //使能定时器3

    NVIC_InitStructure.NVIC_IRQChannel=TIM3_IRQn; //定时器3中断
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=0x01; //抢占优先级1
    NVIC_InitStructure.NVIC_IRQChannelSubPriority=0x03; //子优先级3
    NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}


void TIM3_init(void)
{
    TIM3_Config(999,899);//10ms定时器
    //printf("[%s:%d] TIM3 init!\n",__FUNCTION__,__LINE__);
}


void TIM3_DeInit(void)
{
    // 1. 禁用定时器中断
    TIM_ITConfig(TIM3, TIM_IT_Update, DISABLE);

    // 2. 停止定时器
    TIM_Cmd(TIM3, DISABLE);

    // 3. NVIC 禁用中断通道
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 4. 关掉定时器时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, DISABLE);
}


/**/
void TIM3_sleep_10ms(u32 mTime)
{
    TIM3Delay = mTime;

    while(TIM3Delay != 0);
}


