/*
 * Copyright (c) 2017 Simon Goldschmidt
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Simon Goldschmidt
 *
 */


#include <lwip/opt.h>
#include <lwip/arch.h>
#if !NO_SYS
#include "sys_arch.h"
#endif
#include <lwip/stats.h>
#include <lwip/debug.h>
#include <lwip/sys.h>

#include <string.h>
#include "malloc.h"
#include "os_task.h"

u32_t lwip_sys_now;
extern uint32_t LocalTime;

#define MAX_THREADS_NUM 32

/* 保存所有任务的handle */
typedef struct sys_thread_info{
    sys_thread_t sys_handle_array[MAX_THREADS_NUM];
    uint8_t Index_free; //指向空闲地址
}SYS_THREAD_INFO_T, *LP_SYS_THREAD_INFO;


/* 放内部 BSS：.exram(NOLOAD) 不会被启动代码清零，Index_free 可能是随机值导致踩内存 */
SYS_THREAD_INFO_T sys_handle_info = {0};
#define INDEX_FREE sys_handle_info.Index_free


/* add for rtos */
struct sys_timeouts
{
	struct sys_timeo *next;
};

struct timeoutlist
{
	struct sys_timeouts timeouts;
	xTaskHandle pid;
};

#define SYS_THREAD_MAX 8

static struct timeoutlist s_timeoutlist[SYS_THREAD_MAX];

static u16_t s_nextthread = 0;



u32_t
sys_jiffies(void)
{
  //return lwip_sys_now;
   lwip_sys_now = xTaskGetTickCount();
   return lwip_sys_now;
}

u32_t
sys_now(void)
{

#if NO_SYS
  /* 裸机环境下使用 added by liudayi */
  return LocalTime;
#else
	lwip_sys_now = xTaskGetTickCount();
	return lwip_sys_now;
#endif
}

void
sys_init(void)
{
	 int i;
	 // Initialize the the per-thread sys_timeouts structures
	 // make sure there are no valid pids in the list
	 for (i = 0; i < SYS_THREAD_MAX; i++)
	 {
	     s_timeoutlist[i].pid = 0;
	     s_timeoutlist[i].timeouts.next = NULL;
	 }
	 // keep track of how many threads have been created
	 s_nextthread = 0;
}


struct sys_timeouts *sys_arch_timeouts(void)
{
	int i;
	xTaskHandle pid;
	struct timeoutlist *tl;
	pid = xTaskGetCurrentTaskHandle( );
	for (i = 0; i < s_nextthread; i++)
	{
		tl = &(s_timeoutlist[i]);
		if (tl->pid == pid)
		{
			return &(tl->timeouts);
		}
	}
	return NULL;
}

sys_prot_t sys_arch_protect(void)
{
	vPortEnterCritical();		  //进入临界段
	return 1;
}

void sys_arch_unprotect(sys_prot_t pval)
{
	( void ) pval;
	vPortExitCritical();	  //退出临界段
}

#if !NO_SYS

//test_sys_arch_waiting_fn the_waiting_fn;
//
//void
//test_sys_arch_wait_callback(test_sys_arch_waiting_fn waiting_fn)
//{
//  the_waiting_fn = waiting_fn;
//}

err_t
sys_sem_new(sys_sem_t *sem, u8_t count)
{
#if NO_SYS
//  LWIP_ASSERT("sem != NULL", sem != NULL);
//  *sem = count + 1;
//  return ERR_OK;
#else
	/* 创建 sem */
	if (count <= 1)
	{
		*sem = xSemaphoreCreateBinary();	  //创建二值信号量
		if (count == 1)
		{
			sys_sem_signal(sem);  //新创建的信号量是无效的，需要释放一个信号量
		}
	}
	else
		*sem = xSemaphoreCreateCounting(count,count); //创建计数信号量

#if SYS_STATS
	++lwip_stats.sys.sem.used;
	if (lwip_stats.sys.sem.max < lwip_stats.sys.sem.used)
	{
		lwip_stats.sys.sem.max = lwip_stats.sys.sem.used;
	}
#endif /* SYS_STATS */

	if (*sem != SYS_SEM_NULL)
		return ERR_OK;			//创建成功返回ERR_OK
	else
	{
#if SYS_STATS
		++lwip_stats.sys.sem.err;
#endif /* SYS_STATS */
		printf("[sys_arch]:new sem fail!\n");
		return ERR_MEM;
	}
#endif
}

void
sys_sem_free(sys_sem_t *sem)
{
#if NO_SYS
  LWIP_ASSERT("sem != NULL", sem != NULL);
  *sem = 0;
#else
#if SYS_STATS
	--lwip_stats.sys.sem.used;
#endif /* SYS_STATS */
	/* 删除 sem */
	vSemaphoreDelete(*sem);   //删除一个信号量
	*sem = SYS_SEM_NULL;  //删除之后置空
#endif
}

int
sys_sem_valid(sys_sem_t *sem)
{
//  LWIP_ASSERT("sem != NULL", sem != NULL);
//  *sem = 0;
  
   return (*sem != SYS_SEM_NULL);	 //返回信号量是否有效
}


void
sys_sem_set_invalid(sys_sem_t *sem)
{
//  LWIP_ASSERT("sem != NULL", sem != NULL);
//  *sem = 0;
  
	*sem = SYS_SEM_NULL;    //信号量设置为无效
}


/* semaphores are 1-based because RAM is initialized as 0, which would be valid */
u32_t
sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
#if NO_SYS
//  u32_t ret = 0;
//  LWIP_ASSERT("sem != NULL", sem != NULL);
//  LWIP_ASSERT("*sem > 0", *sem > 0);
//  if (*sem == 1) {
//    /* need to wait */
//    if(!timeout)
//    {
//      /* wait infinite */
//      LWIP_ASSERT("cannot wait without waiting callback", the_waiting_fn != NULL);
//      do {
//        int expectSomething = the_waiting_fn(sem, NULL);
//        LWIP_ASSERT("*sem > 0", *sem > 0);
//        LWIP_ASSERT("expecting a semaphore count but it's 0", !expectSomething || (*sem > 1));
//        ret++;
//        if (ret == SYS_ARCH_TIMEOUT) {
//          ret--;
//        }
//      } while(*sem == 1);
//    }
//    else
//    {
//      if (the_waiting_fn) {
//        int expectSomething = the_waiting_fn(sem, NULL);
//        LWIP_ASSERT("expecting a semaphore count but it's 0", !expectSomething || (*sem > 1));
//      }
//      LWIP_ASSERT("*sem > 0", *sem > 0);
//      if (*sem == 1) {
//        return SYS_ARCH_TIMEOUT;
//      }
//      ret = 1;
//    }
//  }
//  LWIP_ASSERT("*sem > 0", *sem > 0);
//  (*sem)--;
//  LWIP_ASSERT("*sem > 0", *sem > 0);
//  /* return the time we waited for the sem */
//  return ret;
#else
	 u32_t wait_tick = 0;
	 u32_t start_tick = 0 ;

	 //看看信号量是否有效
	 if (*sem == SYS_SEM_NULL)
	     return SYS_ARCH_TIMEOUT;

	 //首先获取开始等待信号量的时钟节拍
	 start_tick = xTaskGetTickCount();

	 //timeout != 0，需要将ms换成系统的时钟节拍
	 if (timeout != 0)
	 {
	     //将ms转换成时钟节拍
	     wait_tick = timeout / portTICK_PERIOD_MS;
	     if (wait_tick == 0)
	         wait_tick = 1;
	 }
	 else
	     wait_tick = portMAX_DELAY;  //一直阻塞

	 //等待成功，计算等待的时间，否则就表示等待超时
	 if (xSemaphoreTake(*sem, wait_tick) == pdTRUE)
	     return ((xTaskGetTickCount()-start_tick)*portTICK_RATE_MS);
	 else
	     return SYS_ARCH_TIMEOUT;
#endif
}

void
sys_sem_signal(sys_sem_t *sem)
{
#if NO_SYS
  LWIP_ASSERT("sem != NULL", sem != NULL);
  LWIP_ASSERT("*sem > 0", *sem > 0);
  (*sem)++;
  LWIP_ASSERT("*sem > 0", *sem > 0);
#else
	if (xSemaphoreGive( *sem ) != pdTRUE)	//释放信号量
		printf("[sys_arch]:sem signal fail!\n");

#endif
}

#define SYS_MRTEX_NULL 0

err_t
sys_mutex_new(sys_mutex_t *mutex)
{
#if NO_SYS
  LWIP_ASSERT("mutex != NULL", mutex != NULL);
  *mutex = 1; /* 1 allocated */
  return ERR_OK;
#else
	/* 创建 sem */
	*mutex = xSemaphoreCreateMutex(); //创建互斥量
	if (*mutex != SYS_MRTEX_NULL)
	 	return ERR_OK;      //创建成功返回ERR_OK
	else
	{
		printf("[sys_arch]:new mutex fail!\n");
	 	return ERR_MEM;
	}
#endif
}

void
sys_mutex_free(sys_mutex_t *mutex)
{
//  /* parameter check */
//  LWIP_ASSERT("mutex != NULL", mutex != NULL);
//  LWIP_ASSERT("*mutex >= 1", *mutex >= 1);
//  *mutex = 0;

   vSemaphoreDelete(*mutex);   //删除互斥量
}

void
sys_mutex_set_invalid(sys_mutex_t *mutex)
{
//  LWIP_ASSERT("mutex != NULL", mutex != NULL);
//  *mutex = 0;

	*mutex = SYS_MRTEX_NULL;	//设置互斥量为无效
}

void
sys_mutex_lock(sys_mutex_t *mutex)
{
//  /* nothing to do, no multithreading supported */
//  LWIP_ASSERT("mutex != NULL", mutex != NULL);
//  /* check that the mutext is valid and unlocked (no nested locking) */
//  LWIP_ASSERT("*mutex >= 1", *mutex == 1);
//  /* we count up just to check the correct pairing of lock/unlock */
//  (*mutex)++;
//  LWIP_ASSERT("*mutex >= 1", *mutex >= 1);

	xSemaphoreTake(*mutex,/* 互斥量句柄 */
					portMAX_DELAY); /* 等待时间 */
}

void
sys_mutex_unlock(sys_mutex_t *mutex)
{
//  /* nothing to do, no multithreading supported */
//  LWIP_ASSERT("mutex != NULL", mutex != NULL);
//  LWIP_ASSERT("*mutex >= 1", *mutex >= 1);
//  /* we count down just to check the correct pairing of lock/unlock */
//  (*mutex)--;
//  LWIP_ASSERT("*mutex >= 1", *mutex >= 1);

	xSemaphoreGive( *mutex );//给出互斥量
}


sys_thread_t
sys_thread_new(const char *name, lwip_thread_fn function, void *arg, int stacksize, int prio)
{
//  LWIP_UNUSED_ARG(name);
//  LWIP_UNUSED_ARG(function);
//  LWIP_UNUSED_ARG(arg);
//  LWIP_UNUSED_ARG(stacksize);
//  LWIP_UNUSED_ARG(prio);
//  /* threads not supported */
//  return 0;

	sys_thread_t handle = (sys_thread_t)os_task_spawn(name, (os_task_fn_t)function, arg,
	                                                (uint16_t)stacksize, (uint8_t)prio);
	if (handle == NULL)
	{
	 	printf("[sys_arch]:create task fail!\n");
	 	return NULL;
	}

    /* 将 handle 加入全局数组；防越界 */
    if (INDEX_FREE < MAX_THREADS_NUM) {
        sys_handle_info.sys_handle_array[INDEX_FREE] = handle;
        INDEX_FREE += 1;
    }

	return handle;
}

err_t
sys_mbox_new(sys_mbox_t *mbox, int size)
{
//  int mboxsize = size;
//  LWIP_ASSERT("mbox != NULL", mbox != NULL);
//  LWIP_ASSERT("size >= 0", size >= 0);
//  if (size == 0) {
//    mboxsize = 1024;
//  }
//  mbox->head = mbox->tail = 0;
//  mbox->sem = mbox; /* just point to something for sys_mbox_valid() */
//  mbox->q_mem = (void**)malloc(sizeof(void*)*mboxsize);
//  mbox->size = mboxsize;
//  mbox->used = 0;
//
//  memset(mbox->q_mem, 0, sizeof(void*)*mboxsize);
//  return ERR_OK;

	/* 创建一个邮箱 */
	*mbox = xQueueCreate((UBaseType_t ) size,/* 邮箱的长度 */
						(UBaseType_t ) sizeof(void *));/* 消息的大小 */
#if SYS_STATS
	++lwip_stats.sys.mbox.used;
	if (lwip_stats.sys.mbox.max < lwip_stats.sys.mbox.used)
	{
		lwip_stats.sys.mbox.max = lwip_stats.sys.mbox.used;
	}
#endif /* SYS_STATS */
	if (NULL == *mbox)
		return ERR_MEM;   // 创建成功返回ERR_OK

	return ERR_OK;
}

void
sys_mbox_free(sys_mbox_t *mbox)
{
//  /* parameter check */
//  LWIP_ASSERT("mbox != NULL", mbox != NULL);
//  LWIP_ASSERT("mbox->sem != NULL", mbox->sem != NULL);
//  LWIP_ASSERT("mbox->sem == mbox", mbox->sem == mbox);
//  LWIP_ASSERT("mbox->q_mem != NULL", mbox->q_mem != NULL);
//  mbox->sem = NULL;
//  free(mbox->q_mem);
//  mbox->q_mem = NULL;

	if ( uxQueueMessagesWaiting( *mbox ) )
	{
		/* Line for breakpoint.  Should never break here! */
		portNOP();
#if SYS_STATS
		lwip_stats.sys.mbox.err++;
#endif /* SYS_STATS */
	}

	vQueueDelete(*mbox);	//删除一个邮箱

#if SYS_STATS
	--lwip_stats.sys.mbox.used;
#endif /* SYS_STATS */
}


int sys_mbox_valid(sys_mbox_t *mbox)
{
	if (*mbox == SYS_MBOX_NULL) 	//判断邮箱是否有效
		return 0;
	else
		return 1;
}


void
sys_mbox_set_invalid(sys_mbox_t *mbox)
{
//  LWIP_ASSERT("mbox != NULL", mbox != NULL);
//  LWIP_ASSERT("mbox->q_mem == NULL", mbox->q_mem == NULL);
//  mbox->sem = NULL;
//  mbox->q_mem = NULL;

	*mbox = SYS_MBOX_NULL;	//设置有效为无效状态
}

void
sys_mbox_post(sys_mbox_t *q, void *msg)
{
//  LWIP_ASSERT("q != SYS_MBOX_NULL", q != SYS_MBOX_NULL);
//  LWIP_ASSERT("q->sem == q", q->sem == q);
//  LWIP_ASSERT("q->q_mem != NULL", q->q_mem != NULL);
//  LWIP_ASSERT("q->used >= 0", q->used >= 0);
//  LWIP_ASSERT("q->size > 0", q->size > 0);
//
//  LWIP_ASSERT("mbox already full", q->used < q->size);
//
//  q->q_mem[q->head] = msg;
//  q->head++;
//  if (q->head >= (unsigned int)q->size) {
//    q->head = 0;
//  }
//  LWIP_ASSERT("mbox is full!", q->head != q->tail);
//  q->used++;

	while (xQueueSend( *q, /* 邮箱的句柄 */
						&msg,/* 发送的消息内容 */
						portMAX_DELAY) != pdTRUE); /* 等待时间 */

}

err_t
sys_mbox_trypost(sys_mbox_t *q, void *msg)
{
//  LWIP_ASSERT("q != SYS_MBOX_NULL", q != SYS_MBOX_NULL);
//  LWIP_ASSERT("q->sem == q", q->sem == q);
//  LWIP_ASSERT("q->q_mem != NULL", q->q_mem != NULL);
//  LWIP_ASSERT("q->used >= 0", q->used >= 0);
//  LWIP_ASSERT("q->size > 0", q->size > 0);
//  LWIP_ASSERT("q->used <= q->size", q->used <= q->size);
//
//  if (q->used == q->size) {
//    return ERR_MEM;
//  }
//  sys_mbox_post(q, msg);
//  return ERR_OK;

	if (xQueueSend(*q,&msg,0) == pdPASS) //尝试发送一个消息，非阻塞发送
		return ERR_OK;
	else
		return ERR_MEM;
}

err_t
sys_mbox_trypost_fromisr(sys_mbox_t *q, void *msg)
{
  //return sys_mbox_trypost(q, msg);
	uint32_t ulReturn;
	err_t err = ERR_MEM;
	BaseType_t pxHigherPriorityTaskWoken;

	/* 进入临界段，临界段可以嵌套 */
	ulReturn = taskENTER_CRITICAL_FROM_ISR();

	if (xQueueSendFromISR(*q,&msg,&pxHigherPriorityTaskWoken)==pdPASS)
	{
		err = ERR_OK;
	}
	//如果需要的话进行一次线程切换
	portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);

	/* 退出临界段 */
	taskEXIT_CRITICAL_FROM_ISR( ulReturn );

	return err;
}

u32_t
sys_arch_mbox_fetch(sys_mbox_t *q, void **msg, u32_t timeout)
{
//  u32_t ret = 0;
//  u32_t ret2;
//  LWIP_ASSERT("q != SYS_MBOX_NULL", q != SYS_MBOX_NULL);
//  LWIP_ASSERT("q->sem == q", q->sem == q);
//  LWIP_ASSERT("q->q_mem != NULL", q->q_mem != NULL);
//  LWIP_ASSERT("q->used >= 0", q->used >= 0);
//  LWIP_ASSERT("q->size > 0", q->size > 0);
//
//  if (q->used == 0) {
//    /* need to wait */
//    /* need to wait */
//    if(!timeout)
//    {
//      /* wait infinite */
//      LWIP_ASSERT("cannot wait without waiting callback", the_waiting_fn != NULL);
//      do {
//        int expectSomething = the_waiting_fn(NULL, q);
//        LWIP_ASSERT("q->used >= 0", q->used >= 0);
//        LWIP_ASSERT("expecting item available but it's 0", !expectSomething || (q->used > 0));
//        ret++;
//        if (ret == SYS_ARCH_TIMEOUT) {
//          ret--;
//        }
//      } while(q->used == 0);
//    }
//    else
//    {
//      if (the_waiting_fn) {
//        int expectSomething = the_waiting_fn(NULL, q);
//        LWIP_ASSERT("expecting item available count but it's 0", !expectSomething || (q->used > 0));
//      }
//      LWIP_ASSERT("q->used >= 0", q->used >= 0);
//      if (q->used == 0) {
//        if(msg) {
//          *msg = NULL;
//        }
//        return SYS_ARCH_TIMEOUT;
//      }
//      ret = 1;
//    }
//  }
//  LWIP_ASSERT("q->used > 0", q->used > 0);
//  ret2 = sys_arch_mbox_tryfetch(q, msg);
//  LWIP_ASSERT("got no message", ret2 == 0);
//  return ret;

	void *dummyptr;
		u32_t wait_tick = 0;
		u32_t start_tick = 0 ;
	
		if ( msg == NULL )	//看看存储消息的地方是否有效
			msg = &dummyptr;
	
		//首先获取开始等待信号量的时钟节拍
		start_tick = sys_now();
	
		//timeout != 0，需要将ms换成系统的时钟节拍
		if (timeout != 0)
		{
			//将ms转换成时钟节拍
			wait_tick = timeout / portTICK_PERIOD_MS;
			if (wait_tick == 0)
				wait_tick = 1;
		}
		//一直阻塞
		else
			wait_tick = portMAX_DELAY;
	
		//等待成功，计算等待的时间，否则就表示等待超时
		if (xQueueReceive(*q,&(*msg), wait_tick) == pdTRUE)
			return ((sys_now() - start_tick)*portTICK_PERIOD_MS);
		else
		{
			*msg = NULL;
			return SYS_ARCH_TIMEOUT;
		}

}

u32_t
sys_arch_mbox_tryfetch(sys_mbox_t *q, void **msg)
{
//  LWIP_ASSERT("q != SYS_MBOX_NULL", q != SYS_MBOX_NULL);
//  LWIP_ASSERT("q->sem == q", q->sem == q);
//  LWIP_ASSERT("q->q_mem != NULL", q->q_mem != NULL);
//  LWIP_ASSERT("q->used >= 0", q->used >= 0);
//  LWIP_ASSERT("q->size > 0", q->size > 0);
//
//  if (!q->used) {
//    return SYS_ARCH_TIMEOUT;
//  }
//  if(msg) {
//    *msg = q->q_mem[q->tail];
//  }
//
//  q->tail++;
//  if (q->tail >= (unsigned int)q->size) {
//    q->tail = 0;
//  }
//  q->used--;
//  LWIP_ASSERT("q->used >= 0", q->used >= 0);
//  return 0;

	void *dummyptr;
	if ( msg == NULL )
	   msg = &dummyptr;

	//等待成功，计算等待的时间
	if (xQueueReceive(*q,&(*msg), 0) == pdTRUE)
	   return ERR_OK;
	else
	   return SYS_MBOX_EMPTY;
}

#if LWIP_NETCONN_SEM_PER_THREAD
#error LWIP_NETCONN_SEM_PER_THREAD==1 not supported
#endif /* LWIP_NETCONN_SEM_PER_THREAD */

#endif /* !NO_SYS */
