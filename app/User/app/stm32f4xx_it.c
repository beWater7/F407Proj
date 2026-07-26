/**
  ******************************************************************************
  * @file    Project/STM32F4xx_StdPeriph_Templates/stm32f4xx_it.c 
  * @author  MCD Application Team
  * @version V1.8.0
  * @date    04-November-2016
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2016 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_usart.h"

#include "core_cm4.h" // ???? Cortex-M4 ??????
//#include "myTaskSchedule.h"
#include "rx_data_queue.h" //???????????added by ldy
#include "typedef.h"

#include "FreeRTOS.h"		  //FreeRTOS???
#include "task.h"
#ifdef __GNUC__
#include "portmacro.h"
#endif


extern __IO u32 TimingDelay;
//extern void hard_fault_handler_c(uint32_t *stack) __attribute__((used));

/** @addtogroup Template_Project
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}



#if 0

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
//  /* Go to infinite loop when Hard Fault exception occurs */
//  while (1)
//  {
//  }

      __asm volatile
    (
        "TST lr, #4               \n"  // ??????????????????
        "ITE EQ                   \n"
        "MRSEQ r0, MSP            \n"  // ???????????
        "MRSNE r0, PSP            \n"  // ???????????
        "B hard_fault_handler_c   \n"  // ????? C ????
    );
}
#endif

//__attribute__((naked)) void HardFault_Handler(void)
//{
//      extern hard_fault_handler_c;
//    __asm volatile (
//        "TST lr, #4               \n"  // ??????????????????
//        "ITE EQ                   \n"  // ???????
//        "MRSEQ r0, MSP            \n"  // ???????????
//        "MRSNE r0, PSP            \n"  // ???????????
//        "B hard_fault_handler_c   \n"  // ????? C ????
//    );
//}
#ifdef __GNUC__
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile (
        "TST LR, #4      \n"
        "ITE EQ          \n"
        "MRSEQ R0, MSP   \n"
        "MRSNE R0, PSP   \n"
        "B hard_fault_handler_c\n"
    );
}
#else
__asm void HardFault_Handler(void)
{
    /* ?????????????????, ????????????? */
    extern hard_fault_handler_c;
    TST LR, #4               // ????????
    ITE EQ                   // ???????
    MRSEQ r0, MSP            // ???????????
    MRSNE r0, PSP            // ???????????
    B hard_fault_handler_c   // ????? C ????
}
#endif
//__asm void HardFault_Handler(void)
//{
//    /* *INDENT-OFF* */
//    PRESERVE8
//
//    /* ???n???????????????? */
//    TST LR, #4                 // ?????????????????????????????
//    ITE EQ                     // ?????????EQ????????
//    MRSEQ r0, MSP              // ???????????????????????
//    MRSNE r0, PSP              // ??????????????????????
//
//    /* ???? r0 ??????????????????????? */
//    /* ???????????????? r4-r11 ?????????? r14 */
//    ldmia r0!, {r4-r11, r14}   // ?????????????? r4-r11 ?? lr
//
//    /* ????????????PSP?? */
//    msr psp, r0               // ??? PSP ?????
//
//    /* ???? C ??????????????? */
//    isb                       // ???????????????????????
//    mov r0, #0                // ????????????????
//    msr basepri, r0           // ????????????
//
//    /* ????? C ??????????????? */
//    bx r14                     // ????? C ????????????????
//
//    /* *INDENT-ON* */
//}

#if 0
__asm void HardFault_Handler(void)
{
    extern hard_fault_handler_c;

#if 1
    TST LR, #4                 // ????????
    ITE EQ                     // ???????
    MRSEQ r0, MSP              // ???????????????????????
    MRSNE r0, PSP              // ??????????????????????

    LDR r1, =hard_fault_handler_c // ?????????????? C ???????
    bl r1                         // ????? C ????
#endif
//      stmdb sp!, {r0, r3}       // ?????????????????
//      mov r0, #3                // ????????????????????????????????
//      msr basepri, r0
//      dsb
//      isb
//      bl hard_fault_handler_c   // ???????????? C ????????
//      mov r0, #0                // ??? BASEPRI?????????????
//      msr basepri, r0
//      ldmia sp!, {r0, r3}       // ??????????????

//    TST lr, #4                 // ????????
//    ITE EQ                     // ???????
//    MRSEQ r0, MSP              // ???????????
//    MRSNE r0, PSP              // ???????????
//
//    PUSH {r4-r11, lr}          // ?????????????
//    BL hard_fault_handler_c    // ???????????? C ????????
//    POP {r4-r11, lr}           // ?????????
//    BX lr                      // ?????????????
}
#endif

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
  }
}

///**
//  * @brief  This function handles SVCall exception.
//  * @param  None
//  * @retval None
//  */
//void SVC_Handler(void)
//{
//}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

///**
//  * @brief  This function handles PendSVC exception.
//  * @param  None
//  * @retval None
//  */
//void PendSV_Handler(void)
//{
//}


/**
* @brief  This function handles SysTick Handler.
* @param  None
* @retval None
*/
//extern void xPortSysTickHandler(void);


/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
    if (TimingDelay != 0x00) {
        TimingDelay--;
    }

    /* ??????? taskENTER_CRITICAL_FROM_ISR?????? FreeRTOS ????? mask ?????
     * ??????????????????? tick ??????????????????????????? */
#if (INCLUDE_xTaskGetSchedulerState == 1)
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
#endif
    {
        xPortSysTickHandler();
    }
}

void KEY1_IRQHandler(void)
{
  //???????????EXTI Line????
	if(EXTI_GetITStatus(KEY1_INT_EXTI_LINE) != RESET) 
	{
		// LED1 ???		??????
		LED1_TOGGLE;
    //???????????
		EXTI_ClearITPendingBit(KEY1_INT_EXTI_LINE);     
	}  
}

void KEY2_IRQHandler(void)
{
  //???????????EXTI Line????
	if(EXTI_GetITStatus(KEY2_INT_EXTI_LINE) != RESET) 
	{
		// LED2 ???		??????
		LED2_TOGGLE;
    //???????????
		EXTI_ClearITPendingBit(KEY2_INT_EXTI_LINE);     
	}  
}


uint8_t count;
uint8_t ch;
QUEUE_DATA_TYPE * data_p2 = NULL;

extern char node_buff[QUEUE_NODE_NUM][QUEUE_NODE_DATA_LEN];
//char 
uint8_t byTmpLen = 0;


void DEBUG_USART_IRQHandler(void)
{
	/* ???? RX ?? rx_queue???? shell_task ?????? shell_input */
 	uint8_t ucCh;
	QUEUE_DATA_TYPE *data_p = NULL;

	if(USART_GetITStatus(DEBUG_USART,USART_IT_RXNE) != RESET)
	{	
	    count++;
		ucCh  = USART_ReceiveData( DEBUG_USART );
        ch = ucCh;
		data_p = cbWrite((QueueBuffer *)&rx_queue);
        data_p2 = data_p;

		if (data_p != NULL)
		{		
            if (data_p->len < QUEUE_NODE_DATA_LEN)
            {
                *(data_p->head + data_p->len) = ucCh;
                data_p->len++;
            }

            if (ucCh == '\n' || ucCh == '\r' || data_p->len >= QUEUE_NODE_DATA_LEN)
            {
                cbWriteFinish((QueueBuffer *)&rx_queue);
            }
		}
	}
    /* ??????§µ??????¦Ä????????????/????????? */
	if ( USART_GetITStatus( DEBUG_USART, USART_IT_IDLE ) == SET )
	{
            data_p = cbWriteUsing((QueueBuffer *)&rx_queue);
            if (data_p != NULL && data_p->len > 0) {
                cbWriteFinish((QueueBuffer *)&rx_queue);
            }
		    (void)USART_ReceiveData( DEBUG_USART );  /* ?? IDLE???? SR+DR */
	}

}	


void ETH_IRQHandler(void)
{
//	uint32_t ulReturn;
//	/* ????????????????????? */
//	ulReturn = taskENTER_CRITICAL_FROM_ISR();
//
//	/* ??????? */
//	taskEXIT_CRITICAL_FROM_ISR( ulReturn );
}


/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f4xx.s).                                               */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/

/**
  * @}
  */ 


/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
