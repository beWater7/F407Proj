/**
  ******************************************************************************
  * @file    bsp_debug_usart.c
  * @author  fire
  * @version V1.0
  * @date    2015-xx-xx
  * @brief   �ض���c��printf������usart�˿ڣ��жϽ���ģʽ
  ******************************************************************************
  * @attention
  *
  * ʵ��ƽ̨:Ұ��  STM32 F407 ������  
  * ��̳    :http://www.firebbs.cn
  * �Ա�    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */ 
  
#include "bsp_debug_usart.h"


uint8_t rx_buffer[RX_BUFFER_SIZE];    // DMA ���λ�����

 /**
  * @brief  ����Ƕ�������жϿ�����NVIC
  * @param  ��
  * @retval ��
  */
static void NVIC_Configuration(void)
{
  NVIC_InitTypeDef NVIC_InitStructure;
  
  /* Ƕ�������жϿ�������ѡ�� */
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
  
  /* ����USARTΪ�ж�Դ */
  NVIC_InitStructure.NVIC_IRQChannel = DEBUG_USART_IRQ;
  /* �������ȼ�Ϊ1 */
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  /* �����ȼ�Ϊ1 */
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  /* ʹ���ж� */
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  /* ��ʼ������NVIC */
  NVIC_Init(&NVIC_InitStructure);
}


 /**
  * @brief  DEBUG_USART GPIO ����,����ģʽ���á�115200 8-N-1 ���жϽ���ģʽ
  * @param  ��
  * @retval ��
  */
void Debug_USART_Config(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  USART_InitTypeDef USART_InitStructure;
		
  RCC_AHB1PeriphClockCmd(DEBUG_USART_RX_GPIO_CLK|DEBUG_USART_TX_GPIO_CLK,ENABLE);

  /* ʹ�� USART ʱ�� */
  RCC_APB2PeriphClockCmd(DEBUG_USART_CLK, ENABLE);
  
  /* GPIO��ʼ�� */
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;  
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  
  /* ����Tx����Ϊ���ù���  */
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Pin = DEBUG_USART_TX_PIN  ;  
  GPIO_Init(DEBUG_USART_TX_GPIO_PORT, &GPIO_InitStructure);

  /* ����Rx����Ϊ���ù��� */
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Pin = DEBUG_USART_RX_PIN;
  GPIO_Init(DEBUG_USART_RX_GPIO_PORT, &GPIO_InitStructure);
  
 /* ���� PXx �� USARTx_Tx*/
  GPIO_PinAFConfig(DEBUG_USART_RX_GPIO_PORT,DEBUG_USART_RX_SOURCE,DEBUG_USART_RX_AF);

  /*  ���� PXx �� USARTx__Rx*/
  GPIO_PinAFConfig(DEBUG_USART_TX_GPIO_PORT,DEBUG_USART_TX_SOURCE,DEBUG_USART_TX_AF);
  
  /* ���ô�DEBUG_USART ģʽ */
  /* ���������ã�DEBUG_USART_BAUDRATE */
  USART_InitStructure.USART_BaudRate = DEBUG_USART_BAUDRATE;
  /* �ֳ�(����λ+У��λ)��8 */
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  /* ֹͣλ��1��ֹͣλ */
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  /* У��λѡ�񣺲�ʹ��У�� */
  USART_InitStructure.USART_Parity = USART_Parity_No;
  /* Ӳ�������ƣ���ʹ��Ӳ���� */
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  /* USARTģʽ���ƣ�ͬʱʹ�ܽ��պͷ��� */
  USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
  /* ���USART��ʼ������ */
  USART_Init(DEBUG_USART, &USART_InitStructure); 
	
  /* Ƕ�������жϿ�����NVIC���� */
	NVIC_Configuration();
  
	/* bootloader �� CLI / Ymodem / UART_RecvByte ������ѯ RXNE��
	 * ���� RX �ж�ȴû��ʵ�� USART1_IRQHandler���������䵽 Default_Handler��
	 * ���ⰴ�����Ῠ��������Ϊ�������� CLI���� */
	USART_ITConfig(DEBUG_USART, USART_IT_RXNE, DISABLE);
	
  /* ʹ�ܴ��� */
	USART_Cmd(DEBUG_USART, ENABLE);
}


/**
  * @brief  Runtime baudrate switch of DEBUG_USART (fast YMODEM).
  *         Keeps 8N1/no-flow-control, only re-applies USART_Init with the
  *         new rate; then drains one stale RX byte (clears RXNE).
  */
void Debug_USART_SetBaud(uint32_t baud)
{
    USART_InitTypeDef us;

    /* Let the current frame shift out before changing the clock divider */
    while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TC) == RESET) {
    }
    us.USART_BaudRate = baud;
    us.USART_WordLength = USART_WordLength_8b;
    us.USART_StopBits = USART_StopBits_1;
    us.USART_Parity = USART_Parity_No;
    us.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    us.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(DEBUG_USART, &us);
    /* Clear RXNE so a byte sampled at the boundary is not mistaken for data */
    (void)USART_ReceiveData(DEBUG_USART);
}

#if 1
/**
  * @brief  USART1 RX DMA ����, ���赽�ڴ�(USART1->DR)
  * @param  ��
  * @retval ��
  */
void USART1_DMA_Init(void) {
    // Enable clocks for USART1, DMA1, GPIOA
		RCC_AHB1PeriphClockCmd(DEBUG_USART_RX_GPIO_CLK|DEBUG_USART_TX_GPIO_CLK,ENABLE);
    
	  RCC_APB2PeriphClockCmd(DEBUG_USART_CLK, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);

    // Configure GPIOA pins for USART1
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9;  // TX
    GPIO_InitStruct.GPIO_Mode =  GPIO_Mode_AF;
	  GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;  
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10;  // RX
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    /* ���� PXx �� USARTx_Tx*/
    GPIO_PinAFConfig(DEBUG_USART_RX_GPIO_PORT,DEBUG_USART_RX_SOURCE,DEBUG_USART_RX_AF);

    /*  ���� PXx �� USARTx__Rx*/
    GPIO_PinAFConfig(DEBUG_USART_TX_GPIO_PORT,DEBUG_USART_TX_SOURCE,DEBUG_USART_TX_AF);
    // Configure USART1
    USART_InitTypeDef USART_InitStruct;
    USART_InitStruct.USART_BaudRate = 115200;
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits = USART_StopBits_1;
    USART_InitStruct.USART_Parity = USART_Parity_No;
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &USART_InitStruct);
    USART_Cmd(USART1, ENABLE);
  /* Ƕ�������жϿ�����NVIC���� */
	NVIC_Configuration();

    // Configure DMA for USART1_RX
    DMA_InitTypeDef DMA_InitStruct;
		
		 /* ��λ��ʼ��DMA������ */
  DMA_DeInit(DMA2_Stream5);

  /* ȷ��DMA��������λ��� */
  while (DMA_GetCmdStatus(DMA2_Stream5) != DISABLE)  {
  }

  /* �����֪usart1 rx��Ӧdma2��ͨ��4��������5 */	
  DMA_InitStruct.DMA_Channel = DMA_Channel_4;  
		
		
		/*����DMAԴ���������ݼĴ�����ַ*/
    DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
	  /*�ڴ��ַ(Ҫ����ı�����ָ��)*/
    DMA_InitStruct.DMA_Memory0BaseAddr = (uint32_t)rx_buffer;
	  /* ���򣺴����赽�ڴ� */
    DMA_InitStruct.DMA_DIR = DMA_DIR_PeripheralToMemory;
	  /*�����СDMA_BufferSize=SENDBUFF_SIZE*/
    DMA_InitStruct.DMA_BufferSize = RX_BUFFER_SIZE;
	  /*�����ַ����*/	 
    DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	  /*�ڴ��ַ����*/
    DMA_InitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
	  /*�������ݵ�λ*/
    DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	  /*�ڴ����ݵ�λ 8bit*/
    DMA_InitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
		/*DMAģʽ������ѭ��*/
    DMA_InitStruct.DMA_Mode = DMA_Mode_Circular;
		/*���ȼ�����*/
    DMA_InitStruct.DMA_Priority = DMA_Priority_High;
		/*����FIFO*/
		DMA_InitStruct.DMA_FIFOMode = DMA_FIFOMode_Disable;        
		DMA_InitStruct.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;    
		/*�洢��ͻ������ 16������*/
		DMA_InitStruct.DMA_MemoryBurst = DMA_MemoryBurst_Single;    
		/*����ͻ������ 1������*/
		DMA_InitStruct.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;  

    DMA_Init(DMA2_Stream5, &DMA_InitStruct);

    // Enable DMA1 Channel5
    DMA_Cmd(DMA2_Stream5, ENABLE);

    // Enable USART1 DMA receiver
    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);
}
#endif


#if 0
/**
  * @brief  USART1 TX DMA ���ã��ڴ浽����(USART1->DR)
  * @param  ��
  * @retval ��
  */
void USART_DMA_Config(void)
{
  DMA_InitTypeDef DMA_InitStructure;

  /*����DMAʱ��*/
  RCC_AHB1PeriphClockCmd(DEBUG_USART_DMA_CLK, ENABLE);
  
  /* ��λ��ʼ��DMA������ */
  DMA_DeInit(DEBUG_USART_DMA_STREAM);

  /* ȷ��DMA��������λ��� */
  while (DMA_GetCmdStatus(DEBUG_USART_DMA_STREAM) != DISABLE)  {
  }

  /*usart1 tx��Ӧdma2��ͨ��4��������7*/	
  DMA_InitStructure.DMA_Channel = DEBUG_USART_DMA_CHANNEL;  
  /*����DMAԴ���������ݼĴ�����ַ*/
  DMA_InitStructure.DMA_PeripheralBaseAddr = DEBUG_USART_DR_BASE;	 
  /*�ڴ��ַ(Ҫ����ı�����ָ��)*/
  DMA_InitStructure.DMA_Memory0BaseAddr = (u32)SendBuff;
  /*���򣺴��ڴ浽����*/		
  DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;	
  /*�����СDMA_BufferSize=SENDBUFF_SIZE*/	
  DMA_InitStructure.DMA_BufferSize = SENDBUFF_SIZE;
  /*�����ַ����*/	    
  DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable; 
  /*�ڴ��ַ����*/
  DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;	
  /*�������ݵ�λ*/	
  DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
  /*�ڴ����ݵ�λ 8bit*/
  DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;	
  /*DMAģʽ������ѭ��*/
  DMA_InitStructure.DMA_Mode =DMA_Mode_Normal;	 
  /*���ȼ�����*/	
  DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;      
  /*����FIFO*/
  DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;        
  DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;    
  /*�洢��ͻ������ 16������*/
  DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;    
  /*����ͻ������ 1������*/
  DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;    
  /*����DMA2��������7*/		   
  DMA_Init(DEBUG_USART_DMA_STREAM, &DMA_InitStructure);
  
  /*ʹ��DMA*/
  DMA_Cmd(DEBUG_USART_DMA_STREAM, ENABLE);
  
  /* �ȴ�DMA��������Ч*/
  while(DMA_GetCmdStatus(DEBUG_USART_DMA_STREAM) != ENABLE)
  {
  }   
}
#endif

/*****************  ����һ���ַ� **********************/
void Usart_SendByte( USART_TypeDef * pUSARTx, uint8_t ch)
{
	/* ����һ���ֽ����ݵ�USART */
	USART_SendData(pUSARTx,ch);
		
	/* �ȴ��������ݼĴ���Ϊ�� */
	while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);	
}

/*****************  �����ַ��� **********************/
void Usart_SendString( USART_TypeDef * pUSARTx, char *str)
{
	unsigned int k=0;
  do 
  {
      Usart_SendByte( pUSARTx, *(str + k) );
      k++;
  } while(*(str + k)!='\0');
  
  /* �ȴ�������� */
  while(USART_GetFlagStatus(pUSARTx,USART_FLAG_TC)==RESET)
  {}
}

/*****************  ����һ��16λ�� **********************/
void Usart_SendHalfWord( USART_TypeDef * pUSARTx, uint16_t ch)
{
	uint8_t temp_h, temp_l;
	
	/* ȡ���߰�λ */
	temp_h = (ch&0XFF00)>>8;
	/* ȡ���Ͱ�λ */
	temp_l = ch&0XFF;
	
	/* ���͸߰�λ */
	USART_SendData(pUSARTx,temp_h);	
	while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);
	
	/* ���͵Ͱ�λ */
	USART_SendData(pUSARTx,temp_l);	
	while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);	
}

///�ض���c�⺯��printf�����ڣ��ض�����ʹ��printf����
int fputc(int ch, FILE *f)
{
		/* ����һ���ֽ����ݵ����� */
		USART_SendData(DEBUG_USART, (uint8_t) ch);
		
		/* �ȴ�������� */
		while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TXE) == RESET);		
	
		return (ch);
}

///�ض���c�⺯��scanf�����ڣ���д����ʹ��scanf��getchar�Ⱥ���
int fgetc(FILE *f)
{
		/* �ȴ������������� */
		while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_RXNE) == RESET);

		return (int)USART_ReceiveData(DEBUG_USART);
}


void waitUsartSend(USART_TypeDef * usart)
{
    while (!(usart->SR & USART_SR_TC)) 
    {
        /*TODO*/
    }
}


/*********************************************END OF FILE**********************/
