#ifndef __MALLOC_H
#define __MALLOC_H



#include <stdint.h>

 
 
#ifndef NULL
#define NULL 0
#endif

//���������ڴ��
#define SRAMIN	 0		//�ڲ��ڴ��
#define SRAMEX   1		//�ⲿ�ڴ�� 

#define SRAMBANK 	2	  //����֧�ֵ�SRAM����.	


//mem1�ڴ�����趨.mem1��ȫ�����ڲ�SRAM����.
#define MEM1_BLOCK_SIZE			32  	  						              //�ڴ���СΪ32�ֽ�
#define MEM1_MAX_SIZE			  40*1024  						              //�������ڴ� 40K
#define MEM1_ALLOC_TABLE_SIZE	MEM1_MAX_SIZE/MEM1_BLOCK_SIZE 	//�ڴ����С


//mem2�ڴ�����趨.mem2���ڴ�ش����ⲿSRAM����
#define MEM2_BLOCK_SIZE			32  	  						              //�ڴ���СΪ32�ֽ�
#define MEM2_GLOBAL_SIZE    240 *1024                         //Ԥ��240K��ȫ�ֱ���ʹ��
#define MEM2_MAX_SIZE			  960 *1024 - 240 *1024			        //�������ڴ�960K-240K = 720K
#define MEM2_ALLOC_TABLE_SIZE	MEM2_MAX_SIZE/MEM2_BLOCK_SIZE 	//�ڴ����С 
		 
#define __EXRAM __attribute__((section("EXRAM")))
 
//�ڴ����������
struct _m_mallco_dev
{
	void    ( * init ) ( uint8_t );				 //��ʼ��
	uint8_t ( * perused ) ( uint8_t );		 //�ڴ�ʹ����
	uint8_t  * membase [ SRAMBANK ];		   //�ڴ�� ����SRAMBANK��������ڴ�
	uint16_t * memmap [ SRAMBANK ]; 		   //�ڴ����״̬��
	uint8_t    memrdy [ SRAMBANK ]; 			 //�ڴ�����Ƿ����
};
extern struct _m_mallco_dev mallco_dev;	 //��mallco.c���涨��


void mymemset(void *s,uint8_t c,uint32_t count);	    //�����ڴ�
void mymemcpy(void *des,void *src,uint32_t n);        //�����ڴ�     
void my_mem_init(uint8_t memx);				                //�ڴ������ʼ������(��/�ڲ�����)
uint32_t my_mem_malloc(uint8_t memx,uint32_t size);	  //�ڴ����(�ڲ�����)
uint8_t my_mem_free(uint8_t memx,uint32_t offset);		//�ڴ��ͷ�(�ڲ�����)
uint8_t my_mem_perused(uint8_t memx);				          //����ڴ�ʹ����(��/�ڲ�����) 


//�û����ú���
void myfree(uint8_t memx,void *ptr);  			           //�ڴ��ͷ�(�ⲿ����)
void *mymalloc(uint8_t memx,uint32_t size);			       //�ڴ����(�ⲿ����)
void *myrealloc(uint8_t memx,void *ptr,uint32_t size); //���·����ڴ�(�ⲿ����)

/* Added by liudayi */
void *os_exsram_malloc(uint32_t size);
void os_exsram_free(void *ptr); 

#define HTTPD_WEB_FILE_LEN (384U * 1024U) /* web ~320KB */


#endif













