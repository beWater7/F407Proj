#ifndef __IAP_H
#define __IAP_H

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdarg.h>
#include "os_debug.h"
#include "typedef.h"

/* ���º궨�����־�����Ҫ��㻻 */

/* Flash���壬����ʹ�õ�оƬ�޸� */
#define ROM_START              			((uint32_t)0x08000000)
#define ROM_SIZE               			(1024 * 1024)
#define ROM_END                			((uint32_t)(ROM_START + ROM_SIZE))
#define STM32_FLASH_START_ADRESS		ROM_START
#define STM32_FLASH_SIZE				    ROM_SIZE
#define STM32_FLASH_END_ADDRESS			ROM_END


/* Flash�������壬STM32F401CCU6��256KB������ʹ�õ�оƬ�޸� ��ǰ�豸ΪF407ZGT6 1M*/
/* Base address of the Flash sectors */ 
#define ADDR_FLASH_SECTOR_0     ((uint32_t)0x08000000) /* Base address of Sector 0, 16 Kbytes   */
#define ADDR_FLASH_SECTOR_1     ((uint32_t)0x08004000) /* Base address of Sector 1, 16 Kbytes   */
#define ADDR_FLASH_SECTOR_2     ((uint32_t)0x08008000) /* Base address of Sector 2, 16 Kbytes   */
#define ADDR_FLASH_SECTOR_3     ((uint32_t)0x0800C000) /* Base address of Sector 3, 16 Kbytes   */
#define ADDR_FLASH_SECTOR_4     ((uint32_t)0x08010000) /* Base address of Sector 4, 64 Kbytes   */
#define ADDR_FLASH_SECTOR_5     ((uint32_t)0x08020000) /* Base address of Sector 5, 128 Kbytes  */
#define ADDR_FLASH_SECTOR_6     ((uint32_t)0x08040000) /* Base address of Sector 6, 128 Kbytes  */
#define ADDR_FLASH_SECTOR_7     ((uint32_t)0x08060000) /* Base address of Sector 7, 128 Kbytes  */
#define ADDR_FLASH_SECTOR_8     ((uint32_t)0x08080000) /* Base address of Sector 8, 128 Kbytes  */
#define ADDR_FLASH_SECTOR_9     ((uint32_t)0x080A0000) /* Base address of Sector 9, 128 Kbytes  */
#define ADDR_FLASH_SECTOR_10    ((uint32_t)0x080C0000) /* Base address of Sector 10, 128 Kbytes */
#define ADDR_FLASH_SECTOR_11    ((uint32_t)0x080E0000) /* Base address of Sector 11, 128 Kbytes */

#define ADDR_FLASH_SECTOR_12     ((uint32_t)0x08100000) /* Base address of Sector 12, 16 Kbytes  */


/* Bootloader��APP �������壬���ݸ��������޸� */
#define BOOT_START_ADDR			0x08000000		  // FLASH_START_ADDR
#define BOOT_FLASH_SIZE			0x8000			  // 32K
#define APP_START_ADDR			(BOOT_START_ADDR + BOOT_FLASH_SIZE)  //��һ���̼�λ��
#define APP_FLASH_SIZE			0x40000           //256k
#define APP_END_ADDR            APP_START_ADDR + APP_FLASH_SIZE

#define APP2_START_ADDR         0x08060000        //��ʼ��ַ + 1K(header) 
#define UPGRADE_CTRL_ADDR       0x080a0000


// #define APP1_ADDRESS ADDR_FLASH_SECTOR_0 + 0x00008000 //0x08000000
// #define APP2_ADDRESS ADDR_FLASH_SECTOR_7
#define APP1_ADDRESS     (ADDR_FLASH_SECTOR_0 + BOOT_FLASH_SIZE)
#define APP2_ADDRESS      ADDR_FLASH_SECTOR_7

#define FLAG_ADDRESS ADDR_FLASH_SECTOR_11 + 124*1024   //�̼�д����ɺ��ڸõ�ַд���־λ,sector12û����ȡ�����ݣ��ᱻ����
#define FW_WRITTEN_FLAG   ((uint32_t)0x00000017)

//#define FLAG_ADDRESS ADDR_FLASH_SECTOR_0 + 26*1024   //


#define __PLOOC_VA_NUM_ARGS_IMPL(   _0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,  \
                                            _13,_14,_15,_16,__N,...)      __N

/* ��ȡ��θ����ĺ� */
//#define __PLOOC_VA_NUM_ARGS(...)                                                \
//                    __PLOOC_VA_NUM_ARGS_IMPL( 0,##__VA_ARGS__,16,15,14,13,12,11,10,9,   \
//                                              8,7,6,5,4,3,2,1,0)

/* ��ȡ��һ������ */
#define GET_FIRST_ARG(arg1, ...) arg1
#define FIRST_ARG(...) GET_FIRST_ARG(__VA_ARGS__, dummy)


/* �жϵ�ǰ�Ƿ��flag����flag�ǵ�һ������һ��Ϊ����Ϊ1 */
//#define __PLOOC_VA_NUM_ARGS(...)                                                \
//                    return (strcmp(KERN_ERROR,FIRST_ARG(...)) ? 0:1)

#define __PLOOC_VA_NUM_ARGS(...) (strlen(GET_FIRST_ARG(__VA_ARGS__)) == 2 ? 1:0)

// ������־�ȼ�ǰ׺, 3���ֽ�
//#define KERN_ALERT   KERN_LEVEL"0"
//#define KERN_ERROR   KERN_LEVEL"1"
//#define KERN_WARN    KERN_LEVEL"2"
//#define KERN_REPORT  KERN_LEVEL"3"
//#define KERN_INFO    KERN_LEVEL"4"
//#define KERN_TRACE   KERN_LEVEL"5"

#define BOOT_PREFIX  "[BOOT] "
#define BOOT_ALERT   CONNECT(KERN_ALERT, BOOT_PREFIX)
#define BOOT_ERROR   CONNECT(KERN_ERROR, BOOT_PREFIX)
#define BOOT_WARN    CONNECT(KERN_WARN, BOOT_PREFIX)
#define BOOT_REPORT  CONNECT(KERN_REPORT, BOOT_PREFIX)
#define BOOT_INFO    CONNECT(KERN_INFO, BOOT_PREFIX)
#define BOOT_TRACE   CONNECT(KERN_TRACE, BOOT_PREFIX)

#define DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)


#if 0
/* ��ͨ��ӡ */
#define __BOOTLOADER_DEBUG_0(format, ...) do{ \
                                            printf("[boot] " format, ##__VA_ARGS__); \
                                      }while(0)


/* Ŀǰ��֧��ERROR�����ӡ��ERROR�����ӡ�»��ӡ���к� */
//#define __BOOTLOADER_DEBUG_2(flag, format, ...) do{   \
//                                            if ((flag) != NULL && strlen(flag) > 1 && (flag)[1] == '1') { \
//                                                /* ���ڲ���ע��Ҳ��Ҫ���з�������ᱨ�� */ \
//                                                printf("[boot] [%s:%d] "format,__FUNCTION__, __LINE__, ##__VA_ARGS__); \
//                                            }else{ \
//                                                printf("[boot] " format, ##__VA_ARGS__); \
//                                            }     \
//                                          }while(0)

//#define __BOOTLOADER_DEBUG_1(flag, format, ...) do{   \
//                                                if ((flag) != NULL && strlen(flag) == 1 && (flag)[1] == '1') { \
//                                                    printf("[boot] [%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
//                                                } else { \
//                                                    printf("[boot] " format, ##__VA_ARGS__); \
//                                                } \
//                                            } while(0)


#define __BOOTLOADER_DEBUG_1(flag, format, ...) do{   \
                                                        printf("[boot] [%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
                                                } while(0)


#define __BOOTLOADER_DEBUG_3(flag, format, ...) do{   \
                                            if ((flag) != NULL && strlen(flag) > 1 && (flag)[1] == '1') { \
                                                /* ���ڲ���ע��Ҳ��Ҫ���з�������ᱨ�� */ \
                                                printf("[boot] [%s:%d] "format,__FUNCTION__, __LINE__, ##__VA_ARGS__); \
                                            }else{ \
                                                printf("[boot] " format, ##__VA_ARGS__); \
                                            }     \
                                          }while(0)


///* ֧�ֿɱ�����ĺ꺯�� */
//#define BOOTLOADER_DEBUG(...) do{                          \
//    CONNECT2(__BOOTLOADER_DEBUG_, __PLOOC_VA_NUM_ARGS(__VA_ARGS__))(##__VA_ARGS__); \     //()(����)
//    }while(0)


/* ����ɱ�����ĵ��Ժ꣬���ݲ�������ѡ���Ӧ�ĵ��Ժ��� */
//#define BOOTLOADER_DEBUG(...) \
//        CONNECT2(__BOOTLOADER_DEBUG_, __PLOOC_VA_NUM_ARGS(__VA_ARGS__))(__VA_ARGS__)


#define PRINT(X) _Generic((X), \
    int: print_int, \
    double: print_double, \
    char*: print_string \
)(X)

void print_int(int x) {
    printf("Integer: %d\n", x);
}

void print_double(double x) {
    printf("Double: %f\n", x);
}

void print_string(char* x) {
    printf("String: %s\n", x);
}


//#define BOOTLOADER_DEBUG(num, ...)  do{ \
//    printf("[BOOT] "); \
//    va_list args; \
//    va_start(args, num); \
//    for(int i = 0; i < num; i++) { \
//        if(0 == i && !strcmp(KERN_ERROR, va_arg(args, char *)))  \
//        { \
//            printf("[%s:%d] ",__FUNCTION__,__LINE__); \
//            continue; \
//        } \
//       PRINT(va_arg(args, void*)); \
//    } \
//    va_end(args); \
//}while(0)
#endif
#define BOOTLOADER_PREFIX [BOOT]
#define TO_STRING(x) #x
#define BOOT_STRING(x) TO_STRING(x)

/*���ջ���û��ʵ�ֻ������ʵ��������ͨ��ӡ�ʹ����ӡ�����кţ�*/
#define BOOTL_PRINT(format, ...) os_printf_api(format, ##__VA_ARGS__)
#define BOOTLOADER_DEBUG(format, ...) printf("[BOOT] [%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#if 0
#define BOOTLOADER_DEBUG(format, ...) do { \
    const char* new_format = NULL; \
    if ((format) != NULL && strlen(format) > 1 && (format)[1] == '1') { \
        new_format = (format) + 2; \
        /* format������ں��棬��Ϊ����������ᱨ�� */ \
        printf("[boot] [%s:%d] %s", __FUNCTION__, __LINE__, new_format, ##__VA_ARGS__); \
    } else { \
        printf("[boot] %s", (format), ##__VA_ARGS__); \
    } \
} while (0)
#endif


/* ����ģʽ */
#define MAIN_APP  1
#define DUAL_APP  2

#define OTA_MODE  MAIN_APP

#define OTA_REGION_SPI_FLASH  1


#define ota_printf printf
// �������������
#define PROGRESS_BAR_LENGTH 53

//void PrintProgressBar(uint8_t progress);
void PrintProgressBar(uint32_t size, uint32_t total_size);


/* ����ӿ� */
void show_boot_info(void);
uint8_t jump_app(uint32_t app_addr);
uint32_t ReadFirmwareFlag(void);
void SetFirmwareFlag(uint32_t flag);
void CleanFirmwareFlag(uint32_t flag);

/* ����ӿ� */
int stm32_flash_read(uint32_t addr, uint8_t *buf, uint32_t size);
int stm32_flash_write(uint32_t addr, const uint8_t *buf, uint32_t size);
int stm32_flash_erase(uint32_t addr, uint32_t size);
uint32_t GetSector(uint32_t Address);

#endif /* __IAP_H */
