#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "bsp_usart.h"

/**
  * @brief  配置嵌套向量中断控制器NVIC
  * @param  无
  * @retval 无
  */
static void NVIC_Configuration(void)
{
  NVIC_InitTypeDef NVIC_InitStructure;
  
  /* 嵌套向量中断控制器组选择 */
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
  
  /* 配置USART为中断源 */
  NVIC_InitStructure.NVIC_IRQChannel = DEBUG_USART_IRQ;
  /* 抢断优先级为1 */
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  /* 子优先级为1 */
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  /* 使能中断 */
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  /* 初始化配置NVIC */
  NVIC_Init(&NVIC_InitStructure);
}


void Debug_USART_Config(void)
{
	
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	
	/* 第一步： 初始化GPIO PA9 和 PA10  引脚复用为Tx 和 Rx*/
	RCC_AHB1PeriphClockCmd(DEBUG_USART_RX_GPIO_CLK|DEBUG_USART_TX_GPIO_CLK,ENABLE); //使能时钟
	
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;  
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    /* 配置Tx引脚为复用功能  */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Pin = DEBUG_USART_TX_PIN  ;  
    GPIO_Init(DEBUG_USART_TX_GPIO_PORT, &GPIO_InitStructure);

    /* 配置Rx引脚为复用功能 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Pin = DEBUG_USART_RX_PIN;
    GPIO_Init(DEBUG_USART_RX_GPIO_PORT, &GPIO_InitStructure);
	
	/* 选择 GPIO 复用功能 */
	
    /* 连接 PXx 到 USARTx_Tx*/
    GPIO_PinAFConfig(DEBUG_USART_RX_GPIO_PORT,DEBUG_USART_RX_SOURCE,DEBUG_USART_RX_AF);

    /*  连接 PXx 到 USARTx__Rx*/
    GPIO_PinAFConfig(DEBUG_USART_TX_GPIO_PORT,DEBUG_USART_TX_SOURCE,DEBUG_USART_TX_AF);
	
	
	
	/* 第二步：配置串口初始化结构体 */
	
	/* 初始化串口时钟 */
	RCC_APB2PeriphClockCmd(DEBUG_USART_CLK, ENABLE);
	
    /* 配置串DEBUG_USART 模式 */
    /* 波特率设置：DEBUG_USART_BAUDRATE */
    USART_InitStructure.USART_BaudRate = DEBUG_USART_BAUDRATE;
    /* 字长(数据位+校验位)：8 */
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    /* 停止位：1个停止位 */
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    /* 校验位选择：不使用校验 */
    USART_InitStructure.USART_Parity = USART_Parity_No;
    /* 硬件流控制：不使用硬件流 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    /* USART模式控制：同时使能接收和发送 */
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    /* 完成USART初始化配置 */
    USART_Init(DEBUG_USART, &USART_InitStructure);
	
	/* 第三步： 配置串口的接收中断 */
	
	/* 嵌套向量中断控制器NVIC配置 */
	NVIC_Configuration();
	
	/* 使能串口接收中断 */
	USART_ITConfig(DEBUG_USART, USART_IT_RXNE, ENABLE);
	/* 这段不加将无法实现环形缓冲区，缓冲区接收不到数据 */
	USART_ITConfig ( DEBUG_USART, USART_IT_IDLE, ENABLE ); //使能串口总线空闲中断 

    /* 第四步：使能中断 */
    /* 使能串口 */
    USART_Cmd(DEBUG_USART, ENABLE);
	
}

/*****************  发送一个字符 **********************/
void Usart_SendByte( USART_TypeDef * pUSARTx, uint8_t ch)
{
	/* 发送一个字节数据到USART */
	USART_SendData(pUSARTx,ch);
		
	/* 等待发送数据寄存器为空 */
	while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);	//完成发送，会返回1， ==0时循环等待
}

/*****************  发送字符串 **********************/
void Usart_SendString( USART_TypeDef * pUSARTx, char *str)
{
	unsigned int k=0;
  do 
  {
      Usart_SendByte( pUSARTx, *(str + k) );
      k++;
  } while(*(str + k)!='\0');
  
  /* 等待发送完成 */
  while(USART_GetFlagStatus(pUSARTx,USART_FLAG_TC)==RESET)
  {}
}


///重定向c库函数printf到串口，重定向后可使用printf函数
int fputc(int ch, FILE *f)
{
		/* 发送一个字节数据到串口 */
		USART_SendData(DEBUG_USART, (uint8_t) ch);
		
		/* 等待发送完毕 */
		while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TXE) == RESET);		
	
		return (ch);
}

///重定向c库函数scanf到串口，重写向后可使用scanf、getchar等函数
int fgetc(FILE *f)
{
		/* 等待串口输入数据 */
		while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_RXNE) == RESET);

		return (int)USART_ReceiveData(DEBUG_USART);
}


///重定向c库函数scanf到串口，重写向后可使用scanf、getchar等函数
int fgetchar(uint8_t *f)
{
		/* 等待串口输入数据 */
		while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_RXNE) == RESET);

		*f = (uint8_t)USART_ReceiveData(DEBUG_USART);
		printf("%c ",*f);
		return 0;
}

void waitUsartSend(USART_TypeDef * usart)
{
    while (!(usart->SR & USART_SR_TC)) 
    {
        /*TODO*/
    }
}

/* 目前使用usart1 */
/*__attribute__((section(".ram_code")))*/ void usart_api_write(uint8_t *buff, uint32_t len)
{
	int i = 0;
	for(i = 0; i < len; i++)
	{
		/* 发送一个字节数据到串口 */
		USART_SendData(DEBUG_USART, (uint8_t) buff[i]);
		
		/* 等待发送完毕 */
		while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TXE) == RESET);
	}
}


/* 自定义 printf 函数 */
/*__attribute__((section(".ram_code")))*/ int sram_printf(const char *format, ...)
{
    char buffer[128]; // 用于存储格式化后的数据
    va_list args;

    // 初始化变长参数列表
    va_start(args, format);

    // 格式化字符串
    int len = vsnprintf(buffer, sizeof(buffer), format, args);

    // 结束变长参数列表
    va_end(args);

    // 发送数据到串口
    if (len > 0)
    {
        usart_api_write((uint8_t *)buffer, len);
    }

    return len;
}

#if 0
uint32_t usart_api_read(uint8_t *buff, uint32_t len)
{
	int i = 0;
	int waitCnt = 0;
	for(i = 0; i < len; i++)
	{
		/* 等待串口输入数据 */
		if(USART_GetFlagStatus(DEBUG_USART, USART_FLAG_RXNE) != RESET)
		{
			// 读取串口数据并存储到缓冲区
			buff[i] = (uint8_t)USART_ReceiveData(DEBUG_USART);
			waitCnt = 0;
			if(1 == len)
			{
				return i+1;
			}
		}
		else
		{
			if(waitCnt > 1000)
			{
				return i;
			}
			waitCnt++;
			/* 没获取到重新读 */
			i--;
			continue;
		}
		

	}
	return i;
}
#endif

uint32_t usart_api_read(uint8_t *buff, uint32_t len)
{
    uint32_t i = 0;

    for(i = 0; i < len; i++)
    {
        // 如果USART缓冲区有数据可读
        if(USART_GetFlagStatus(DEBUG_USART, USART_FLAG_RXNE) != RESET)
        {
            buff[i] = (uint8_t)USART_ReceiveData(DEBUG_USART);
			printf("%c \n", buff[i]);			
            return i + 1;  // 返回读取的字节数
        }
        else
        {
            return i;  // 没有数据可读，返回
        }
    }

    return i;  // 如果读取了多个字节，就返回读取的总字节数
}


