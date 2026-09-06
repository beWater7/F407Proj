/***************************************************************
 * @file    :  storage_manage.c
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/
 
#include <stdlib.h>
#include <string.h>
#include "dev_manage.h"
#include "os_debug.h"
#include "os_mutex.h"
#include "crc.h"


typedef struct {
    dev_obj *next;
    os_mutex_t lock;
}dev_mgr;

static dev_mgr s_dev_mgr;


/*****************************************************
 * @fn       dev_init
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void dev_init()
{
    os_mutex_init(s_dev_mgr.lock);
    s_dev_mgr.next = NULL;
}


/*****************************************************
 * @fn       dev_get
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void* dev_get(uint8_t dev_id)
{
    dev_obj *curr = s_dev_mgr.next;

    while (curr) 
    {
        if (curr->dev_id == dev_id)
        {
            return curr;
        }
        curr = curr->next;
    }
    return NULL; // 没找到
}


/*****************************************************
 * @fn       ${1:partition_init_all}
 * @brief    ${2:分区初始化}
 * @note     ${3:初始化全部 SPI Flash 分区控制块}
 * @param    ${4:无}
 * @retval   ${5:无}
 *****************************************************/
int dev_register(uint8_t dev_id, dev_obj *dev)
{
    /* 是否已存在 */
    CUSTOM_ASSERT(NULL != dev_get(dev_id), return -1);
    CUSTOM_ASSERT(NULL == dev, return -1);

    dev->dev_id = dev_id;
    os_mutex_lock(s_dev_mgr.lock);
    dev->next = s_dev_mgr.next;
    s_dev_mgr.next = dev;
    os_mutex_unlock(s_dev_mgr.lock);

    return 0;
}


/*****************************************************
 * @fn       dev_unregister
 * @brief    注销设备
 * @note     从设备链表中删除指定 dev_id 的设备节点
 * @param    dev_id 设备 ID
 * @retval   0: 成功, -1: 未找到
 *****************************************************/
int dev_unregister(uint8_t dev_id)
{
    // 定义一个指向指针的指针，指向链表头的 next
    dev_obj **pp = &s_dev_mgr.next;
    os_mutex_lock(s_dev_mgr.lock);
    
    /* pp——>*pp
     *       ||
     *      s_dev_mgr.NEXT(指针)
     *      |
     *      v    
     *      A-B-C-D-E-F(实例)
     *
     * pp——>*pp              pp——>*pp   
     *       ||                    ||
     *      A.NEXT(指针)           B.NEXT(指针)
     *      |                    |
     *      v                    v                       
     *      B-C-D-E-F            C-D-E-F
     *
     */
    while (*pp)  // 当当前节点不为空
    {
        if ((*pp)->dev_id == dev_id) 
        {
            // 找到目标节点
            dev_obj *to_delete = *pp;
            (void)to_delete;

            /* 断链：让前一个节点的 next 指向要删除节点的 next
             * *pp 是指向 B 的指针即为A->next）
             *
             * (*pp)->next：先对 *pp 用->取值，就是 B，然后访问 B->next
             * 所以以下的断链就是 A->next = B->next
             */
            *pp = (*pp)->next;

            // 如果节点是动态分配的，这里要释放
            // free(to_delete);

            printf("Device %d unregistered OK\n", dev_id);
            os_mutex_unlock(s_dev_mgr.lock);
            return 0;
        }

        // 移动到下一个节点（指针指向下一个 next）
        pp = &(*pp)->next;
    }
    os_mutex_unlock(s_dev_mgr.lock);

    printf("Device %d not found\n", dev_id);
    return -1;
}





