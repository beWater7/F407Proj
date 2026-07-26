#include "iap.h"
#include <stdio.h>
#include <string.h>

/*********************************************************************
  +----------------------------+
  |         FLASH              |
  |   (�� .sct/.ld ����)       |
  |                            |
  |  0x08000000  +-------------+   --> ����������ַ (VTOR)
  |               | MSP ��ֵ   | --> *(0x08000000) = 0x20020000 (SRAM��)
  |  0x08000004  +-------------+
  |               | Reset_Handler ��ַ |
  |  0x08000008  +-------------+
  |               | �����ж���� ...   |
  +----------------------------+

                 ��
          ����ʱӲ������
-------------------------------------------
  PC = *(VTOR + 0x04)  --> Reset_Handler
  MSP = *(VTOR + 0x00) --> ջ��
  VTOR = ����������ַ   --> 0x08000000

  (���Bootloader��תApp����)
  SCB->VTOR = App��ʼ��ַ
  __set_MSP(*(App��ʼ��ַ))
-------------------------------------------

  +----------------------------+
  |         SRAM               |
  |                            |
  |  0x20020000  +-------------+   --> ʵ��ջ����ַ (MSP)
  +----------------------------+



����������:

__Vectors       DCD     __initial_sp               ; Top of Stack
                DCD     Reset_Handler              ; Reset Handler
                DCD     NMI_Handler                ; NMI Handler
                DCD     HardFault_Handler          ; Hard Fault Handler
                DCD     MemManage_Handler          ; MPU Fault Handler
                DCD     BusFault_Handler           ; Bus Fault Handler
                DCD     UsageFault_Handler         ; Usage Fault Handler

                ...


**********************************************************************/

/* banner�Զ����ɵ�ַ
 * https://manytools.org/hacker-tools/ascii-banner/
 */

const char* dy_boot_logo1 =
"\n"
"   ,--,                                               \n"
",---.'|                                               \n"
"|   | :       ,---,                                   \n"
":   : |     .'  .' `\\         ,---,                   \n"
"|   ' :   ,---.'     \\       /_ ./|                   \n"
";   ; '   |   |  .`\\  |,---, |  ' :                   \n"
"'   | |__ :   : |  '  /___/ \\.  : |                   \n"
"|   | :.'||   ' '  ;  :.  \\  \\ ,' '                   \n"
"'   :    ;'   | ;  .  | \\  ;  `  ,'                   \n"
"|   |  ./ |   | :  |  '  \\  \\    '                    \n"
";   : ;   '   : | /  ;    '  \\   |                    \n"
"|   ,/    |   | '` ,/      \\  ;  ;                    \n"
"'---'     ;   :  .'         :  \\  \\                   \n"
"          |   ,.'            \\  ' ;                   \n"
"          '---'               `--`             ,----, \n"
"               ,----..       ,----..         ,/   .`| \n"
"    ,---,.    /   /   \\     /   /   \\      ,`   .'  : \n"
"  ,'  .'  \\  /   .     :   /   .     :   ;    ;     / \n"
",---.' .' | .   /   ;.  \\ .   /   ;.  \\'___,/    ,'  \n"
"|   |  |: |.   ;   /  ` ;.   ;   /  ` ;|    :     |   \n"
":   :  :  /;   |  ; \\ ; |;   |  ; \\ ; |;    |.';  ;   \n"
":   |    ; |   :  | ; | '|   :  | ; | '`----'  |  |   \n"
"|   :     \\.   |  ' ' ' :.   |  ' ' ' :    '   :  ;   \n"
"|   |   . |'   ;  \\; /  |'   ;  \\; /  |    |   |  '   \n"
"'   :  '; | \\   \\  ',  /  \\   \\  ',  /     '   :  |   \n"
"|   |  | ;   ;   :    /    ;   :    /      ;   |.'    \n"
"|   :   /     \\   \\ .'      \\   \\ .'       '---'      \n"
"|   | ,'       `---`         `---`                    \n"
"`----'                                                \n";


/* 3D-ASCII */
const char* dy_boot_logo2 =
"\n"
" ___       ________      ___    ___        \n"
"|\\  \\     |\\   ___ \\    |\\  \\  /  /|       \n"
"\\ \\  \\    \\ \\  \\_|\\ \\   \\ \\  \\/  / /       \n"
" \\ \\  \\    \\ \\  \\ \\\\ \\   \\ \\    / /        \n"
"  \\ \\  \\____\\ \\  \\_\\\\ \\   \\/  /  /         \n"
"   \\ \\_______\\ \\_______\\__/  / /           \n"
"    \\|_______|\\|_______|\\___/ /            \n"
"                       \\|___|/             \n"
"                                           \n"
" ________  ________  ________  _________   \n"
"|\\   __  \\|\\   __  \\|\\   __  \\|\\___   ___\\ \n"
"\\ \\  \\|\\ /\\ \\  \\|\\  \\ \\  \\|\\  \\|___ \\  \\_| \n"
" \\ \\   __  \\ \\  \\\\\\  \\ \\  \\\\\\  \\   \\ \\  \\  \n"
"  \\ \\  \\|\\  \\ \\  \\\\\\  \\ \\  \\\\\\  \\   \\ \\  \\ \n"
"   \\ \\_______\\ \\_______\\ \\_______\\   \\ \\__\\\n"
"    \\|_______|\\|_______|\\|_______|    \\|__|\n";

/* �޷�ʹ�õ�
const char* dy_boot_logo3 =
"\n"
" ������    �������������{������   ������           \n"
"������?    ?����? ������?����  ����?           \n"
"?����?    ?����   ���� ?���� ����?           \n"
"?����?    ?�����{   �� ? ?������?           \n"
"?������������???����������  ? ����?��?           \n"
"? ??��  ? ??��  ?   ����???            \n"
"? ? ?  ? ? ?  ? ������ ???            \n"
"  ? ?    ? ?  ? ? ? ??             \n"
"    ?  ?   ?    ? ?                \n"
"         ?      ? ?                \n"
" �{�{�{�{    ?����������   ?����������  �{�{�{������������\n"
"�������������{ ?����?  ����??����?  ����?��  ����? ��?\n"
"?����? �{����?����?  ����??����?  ����?? ������? ??\n"
"?����?��?  ?����   ����??����   ����?? �������� ? \n"
"?����  ?����? ����������??? ����������??  ?����? ? \n"
"??��������??? ?????? ? ??????   ? ??   \n"
"???   ?   ? ? ??   ? ? ??     ?    \n"
" ?    ? ? ? ? ?  ? ? ? ?    ?      \n"
" ?          ? ?      ? ?           \n"
"      ?                             \n";

const char* dy_boot_logo_ansi =
"\n"
"�����[     �������������[ �����[   �����[          \n"
"�����U     �����X�T�T�����[�^�����[ �����X�a          \n"
"�����U     �����U  �����U �^���������X�a           \n"
"�����U     �����U  �����U  �^�����X�a            \n"
"���������������[�������������X�a   �����U             \n"
"�^�T�T�T�T�T�T�a�^�T�T�T�T�T�a    �^�T�a             \n"
"                                   \n"
"�������������[  �������������[  �������������[ �����������������[\n"
"�����X�T�T�����[�����X�T�T�T�����[�����X�T�T�T�����[�^�T�T�����X�T�T�a\n"
"�������������X�a�����U   �����U�����U   �����U   �����U   \n"
"�����X�T�T�����[�����U   �����U�����U   �����U   �����U   \n"
"�������������X�a�^�������������X�a�^�������������X�a   �����U   \n"
"�^�T�T�T�T�T�a  �^�T�T�T�T�T�a  �^�T�T�T�T�T�a    �^�T�a   \n";

*/

const char* dy_boot_slant =
"\n"
" __         _____     __  __              \n"
"/\\ \\       /\\  __-.  /\\ \\_\\ \\             \n"
"\\ \\ \\____  \\ \\ \\/\\ \\ \\ \\____ \\            \n"
" \\ \\_____\\  \\ \\____-  \\/\\_____\\           \n"
"  \\/_____/   \\/____/   \\/_____/           \n"
"                                          \n"
" ______     ______     ______     ______  \n"
"/\\  == \\   /\\  __ \\   /\\  __ \\   /\\__  _\\ \n"
"\\ \\  __<   \\ \\ \\/\\ \\  \\ \\ \\/\\ \\  \\/_/\\ \\/ \n"
" \\ \\_____\\  \\ \\_____\\  \\ \\_____\\    \\ \\_\\ \n"
"  \\/_____/   \\/_____/   \\/_____/     \\/_/ \n"
"                                          \n";




const char* dy_boot_simple =
"\n"
"  _    _____   __      \n"
" | |  |   \\ \\ / /      \n"
" | |__| |) \\ V /       \n"
" |____|___/ |_|_ _____ \n"
" | _ )/ _ \\ / _ \\_   _|\n"
" | _ \\ (_) | (_) || |  \n"
" |___/\\___/ \\___/ |_|  \n"
"                       \n";

const char* dy_boot_slant_tight =
"\n"
" __       _____    __  __            \n"
"/\\ \\     /\\  __-. /\\ \\_\\ \\           \n"
"\\ \\ \\___ \\ \\ \\/\\ \\\\ \\____ \\          \n"
" \\ \\_____\\ \\ \\____-\\/\\_____\\         \n"
"  \\/____/  \\/____/  \\/_____/         \n"
"                                     \n"
" ______   ______   ______   ______   \n"
"/\\  == \\ /\\  __ \\ /\\  __ \\ /\\__  _\\ \n"
"\\ \\  __< \\ \\ \\/\\ \\\\ \\ \\/\\ \\\\/_/\\ \\/ \n"
" \\ \\_____\\\\ \\_____\\\\ \\_____\\  \\ \\_\\ \n"
"  \\/_____/ \\/_____/ \\/_____/   \\/_/ \n"
"                                     \n";

const char* dy_boot_softblock =
"\n"
" _       _____  _     _          \n"
"| |     (____ \\| |   | |         \n"
"| |      _   \\ \\ |___| |         \n"
"| |     | |   | \\_____/          \n"
"| |_____| |__/ /  ___            \n"
"|_______)_____/  (___)           \n"
"                                 \n"
" ______   _____   _____  _______ \n"
"(____  \\ / ___ \\ / ___ \\(_______)\n"
" ____)  ) |   | | |   | |_       \n"
"|  __  (| |   | | |   | | |      \n"
"| |__)  ) |___| | |___| | |_____ \n"
"|______/ \\_____/ \\_____/ \\______)\n"
"                                  \n";




void show_boot_info(void)
{
    
    ota_printf("\r\n");
    ota_printf("%s", dy_boot_logo2);
    //BOOTL_PRINT(KERN_REPORT"%s", dy_boot_logo2);
    BOOTL_PRINT(KERN_REPORT"\n     Version: v1.0.0   Build: %s %s\n", __DATE__, __TIME__);
    //BOOTL_PRINT(BOOT_REPORT"---------- Enter BootLoader ----------\r\n");
    ota_printf("\r\n");
    BOOTL_PRINT(BOOT_INFO" ======= flash pration table ========\r\n");
    BOOTL_PRINT(BOOT_INFO"| name     | offset     | size       |\r\n");
    BOOTL_PRINT(BOOT_INFO"--------------------------------------\r\n");
    BOOTL_PRINT(BOOT_INFO"| boot     | 0x%08X | 0x%08X |        \r\n", BOOT_START_ADDR, BOOT_FLASH_SIZE);
    BOOTL_PRINT(BOOT_INFO"| app      | 0x%08X | 0x%08X |        \r\n", APP_START_ADDR, APP_FLASH_SIZE);
    BOOTL_PRINT(BOOT_INFO"| app2     | 0x%08X | 0x%08X |        \r\n", APP2_START_ADDR, APP_FLASH_SIZE);
    BOOTL_PRINT(BOOT_INFO" ====================================\r\n");
    ota_printf("\r\n");
}

typedef void (*jump_callback)(void);
/**
 * @note ��ת��App����
 *
 * @param App��ʼ��ַ
 *
 * @return result
 */
uint8_t jump_app(uint32_t app_addr)
{
    uint32_t jump_addr;
    uint32_t msp;
    jump_callback cb;

    /* F407ZG �ڲ� SRAM: 0x20000000 ~ 0x2001FFFF (128KB)��
     * ���ӽű����� _estack = ORIGIN+LENGTH = 0x20020000��ջ����һ��Խ�硱�� Cortex-M �������Ϸ�����
     * ST ʾ������ (msp & 0x2FFE0000) == 0x20000000 ��� 0x20020000 ��Ϊ�Ƿ���
     * ���� jump ��Զʧ�ܡ����� boot �� while(1)��APP ��ȫ������� */
    msp = *(volatile uint32_t *)app_addr;
    if (msp >= 0x20000000u && msp <= 0x20020000u)
    {
        /* 复位向量位于程序起始地址+4处, 即 Reset_Handler */
        jump_addr = *(volatile uint32_t *)(app_addr + 4);

        cb = (jump_callback)jump_addr;

        /* 必须先切 VTOR，再跳 APP；否则 APP 里 svc/PendSV/外设中断仍进 Boot 向量表 */
        __disable_irq();
        SCB->VTOR = app_addr;
        __DSB();
        __ISB();

        __set_MSP(msp);
        __enable_irq();

        cb();

        return 1;
    }
    return 0;
}


#if 0
/**
  * @brief  Gets the sector of a given address
  * @param  None
  * @retval The sector of a given address
  */
uint32_t GetSector(uint32_t Address)
{
  uint32_t sector = 0;
  
  if((Address < ADDR_FLASH_SECTOR_1) && (Address >= ADDR_FLASH_SECTOR_0))
  {
    sector = FLASH_Sector_0;  
  }
  else if((Address < ADDR_FLASH_SECTOR_2) && (Address >= ADDR_FLASH_SECTOR_1))
  {
    sector = FLASH_Sector_1;  
  }
  else if((Address < ADDR_FLASH_SECTOR_3) && (Address >= ADDR_FLASH_SECTOR_2))
  {
    sector = FLASH_Sector_2;  
  }
  else if((Address < ADDR_FLASH_SECTOR_4) && (Address >= ADDR_FLASH_SECTOR_3))
  {
    sector = FLASH_Sector_3;  
  }
  else if((Address < ADDR_FLASH_SECTOR_5) && (Address >= ADDR_FLASH_SECTOR_4))
  {
    sector = FLASH_Sector_4;  
  }
  else if((Address < ADDR_FLASH_SECTOR_6) && (Address >= ADDR_FLASH_SECTOR_5))
  {
    sector = FLASH_Sector_5;  
  }
  else if((Address < ADDR_FLASH_SECTOR_7) && (Address >= ADDR_FLASH_SECTOR_6))
  {
    sector = FLASH_Sector_6;  
  }
  else if((Address < ADDR_FLASH_SECTOR_8) && (Address >= ADDR_FLASH_SECTOR_7))
  {
    sector = FLASH_Sector_7;  
  }
  else if((Address < ADDR_FLASH_SECTOR_9) && (Address >= ADDR_FLASH_SECTOR_8))
  {
    sector = FLASH_Sector_8;  
  }
  else if((Address < ADDR_FLASH_SECTOR_10) && (Address >= ADDR_FLASH_SECTOR_9))
  {
    sector = FLASH_Sector_9;  
  }
  else if((Address < ADDR_FLASH_SECTOR_11) && (Address >= ADDR_FLASH_SECTOR_10))
  {
    sector = FLASH_Sector_10;  
  }
  else /*if((Address < FLASH_END_ADDR) && (Address >= ADDR_FLASH_SECTOR_11))*/
  {
    sector = FLASH_Sector_11;  
  }

  return sector;
}


uint32_t ReadFirmwareFlag(void) {
    /* ��ȡ����Ҫ��������д��Ҫ */
    return *(uint32_t*)(FLAG_ADDRESS-4);
}


void SetFirmwareFlag(uint32_t flag) {
    uint32_t uwStartSector = 0;

    FLASH_Unlock();

    /* �����û����� (�û�����ָ������û��ʹ�õĿռ䣬�����Զ���)**/
    /* �������FLASH�ı�־λ */  
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                  FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR);

#if 0
    /* ��ȡ�̼�������ʼ��ַ�ͽ�����ַ */
    uwStartSector = GetSector(ADDR_FLASH_SECTOR_12);
    /* VoltageRange_3 �ԡ���(32λ)���Ĵ�С���в�����������������Ŀռ� */
    if (FLASH_EraseSector(uwStartSector, VoltageRange_3) != FLASH_COMPLETE)
    {
        BOOTLOADER_DEBUG("FLASH_EraseSector err!\n");
        /*�������������أ�ʵ��Ӧ���пɼ��봦�� */
        return;
    }
#endif
    if (FLASH_ProgramWord(FLAG_ADDRESS, FW_WRITTEN_FLAG) != FLASH_COMPLETE)
    {
        BOOTLOADER_DEBUG("FLASH byte write error at start!\n");
        return;
    }

    /* ��FLASH��������ֹ���ݱ��۸�*/
    FLASH_Lock();
    return;
}


void CleanFirmwareFlag(uint32_t flag) {
    uint32_t uwStartSector = 0;

    FLASH_Unlock();

    /* �����û����� (�û�����ָ������û��ʹ�õĿռ䣬�����Զ���)**/
    /* �������FLASH�ı�־λ */  
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                  FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR);
#if 0
    /* ��ȡ�̼�������ʼ��ַ�ͽ�����ַ */
    uwStartSector = GetSector(ADDR_FLASH_SECTOR_11);
    /* VoltageRange_3 �ԡ���(32λ)���Ĵ�С���в�����������������Ŀռ� */
    if (FLASH_EraseSector(uwStartSector, VoltageRange_3) != FLASH_COMPLETE)
    {
        BOOTLOADER_DEBUG("FLASH_EraseSector err!\n");
        /*�������������أ�ʵ��Ӧ���пɼ��봦�� */
        return;
    }
#endif
    if (FLASH_ProgramWord(FLAG_ADDRESS, 0x00000000) != FLASH_COMPLETE)
    {
        BOOTLOADER_DEBUG("FLASH byte write error at start!\n");
        return;
    }

    /* ��FLASH��������ֹ���ݱ��۸�*/
    FLASH_Lock();
    return;
}
#endif

// ��ӡ������
void PrintProgressBar(uint32_t size, uint32_t total_size)
{
    int filled_length = 0;
    char bar[PROGRESS_BAR_LENGTH + 1] = {0};
    static uint8_t preProgress = 0;
    uint8_t progress = 0;
    uint8_t i = 0;

    progress = ((double)size/total_size*100.0 + 0.5);

    //printf("progress %d\n", progress);
    if(progress < 0 || progress > 100)
    {
        return;
    }

    /* �ظ���ֱ��return */
    if(preProgress == progress)
    {
        return;
    }
    preProgress = progress;

    /* ÿ����5�����ӡһ�ν��ȱ� */
    if(0 != (progress%5))
    {
        return;
    }

    filled_length = (progress * PROGRESS_BAR_LENGTH) / 100;
    memset(bar, ' ', PROGRESS_BAR_LENGTH);
    bar[PROGRESS_BAR_LENGTH] = '\0'; // ȷ���ַ�����'\0'��β
    /* ѭ��д�� */
    for (i = 0; i < filled_length; i++)
    {
        bar[i] = '#';
    }

    // ʹ��\r�ص����ף���ӡ�������Ͱٷֱ�
    ota_printf( "\r[%s] %d%%", bar, progress);
    if(100 == progress)
    {
        ota_printf("\n\n");
    }
    //ota_printf( "[%s] %d%%\n", bar, progress);
}


/**
 * Read data from flash.
 * @note This operation's units is word.
 *
 * @param addr flash address
 * @param buf buffer to store read data
 * @param size read bytes size
 *
 * @return result
 */
int stm32_flash_read(uint32_t addr, uint8_t *buf, uint32_t size)
{
    size_t i;

    if ((addr + size) > STM32_FLASH_END_ADDRESS)
    {
        BOOTLOADER_DEBUG("read outrange flash size! addr is (0x%p)", (void*)(addr + size));
        return -1;
    }

    for (i = 0; i < size; i++, buf++, addr++)
    {
        *buf = *(uint8_t *) addr;
    }

    return size;
}


/**
 * Write data to flash.
 * @note This operation's units is word.
 * @note This operation must after erase. @see flash_erase.
 *
 * @param addr flash address
 * @param buf the write data buffer
 * @param size write bytes size
 *
 * @return result
 */
int stm32_flash_write(uint32_t addr, const uint8_t *buf, uint32_t size)
{
    int8_t result = 0;
    uint32_t end_addr = addr + size;

		/* д���ַ�ͳ���У�� */
    if ((end_addr) > STM32_FLASH_END_ADDRESS)
    {
        BOOTLOADER_DEBUG("write outrange flash size! addr is (0x%p)", (void*)(addr + size));
        return -1;
    }

    if (size < 1)
    {
        return -1;
    }

    FLASH_Unlock();

    /* �������FLASH�ı�־λ */  
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                  FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR); 

	/* ���ֽ�д�����ݵ��ڲ�flash����ʵ���԰�16λ��32λд�룬���Ǵ���������д����Ҫ����δ�������� */
    for (size_t i = 0; i < size; i++, addr++, buf++)
    {
        /* write data to flash */
        if (FLASH_COMPLETE == FLASH_ProgramByte(addr, (uint64_t)(*buf)))
        {
            if (*(uint8_t *)addr != *buf)
            {
                result = -1;
                break;
            }
        }
        else
        {
            result = -1;
            break;
        }
    }

    FLASH_Lock();

    return result;
}

/**
 * Erase data on flash.
 * @note This operation is irreversible.
 * @note This operation's units is different which on many chips.
 *
 * @param addr flash address
 * @param size erase bytes size
 *
 * @return result
 */
int stm32_flash_erase(uint32_t addr, uint32_t size)
{
    int8_t result = 0;
    uint32_t FirstSector = 0, LastSector = 0, NbOfSectors = 0;
    uint32_t SECTORError = 0;

    if ((addr + size) > STM32_FLASH_END_ADDRESS)
    {
        BOOTLOADER_DEBUG("ERROR: erase outrange flash size! addr is (0x%p)\n", (void*)(addr + size));
        return -1;
    }

    /* Unlock the Flash to enable the flash control register access */
    FLASH_Unlock();

    /* �������FLASH�ı�־λ */  
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                  FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR); 
		
		/* Get the 1st sector to erase */
    FirstSector = GetSector(addr);
    
    /* Get the number of sector to erase from 1st sector*/
    LastSector = GetSector(addr + size - 1);
		
    while (FirstSector <= LastSector) 
    {
        //printf("flash(rom) SectorCounter:%x erased!\n", FirstSector);
        printf("#");
        /* VoltageRange_3 �ԡ���(32λ)���Ĵ�С���в�����������������Ŀռ� */ 
        if (FLASH_EraseSector(FirstSector, VoltageRange_3) != FLASH_COMPLETE)
        {
            BOOTLOADER_DEBUG("FLASH_EraseSector err!\n");
            /*�������������أ�ʵ��Ӧ���пɼ��봦�� */
            return -1;
        }

        /* ������ָ����һ������ */
        if (FirstSector == FLASH_Sector_11)
        {
            /* sector11-sector12֮��ļ����40 */
            FirstSector += 40;
        } 
        else
        {
            /* ��������֮��ļ����8 */
            FirstSector += 8;
        }
    }
    FLASH_Lock();
    printf("\n");
    BOOTLOADER_DEBUG("flash(rom) erase success!\n");
    return 0;
}		
		
