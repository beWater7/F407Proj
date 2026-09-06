/*
 * ymodem.c
 *
 *  Created on: 2017年9月21日
 *      Author: Administrator
 *    Function:和Ymodem.c的相关的协议文件。负责从超级终端接收数据(使用Ymodem协议)，并将数据加载到内部RAM中。
 *             如果接收数据正常，则将数据编程到Flash中；如果发生错误，则提示出错。
 */
#include "include.h"
#include "bsp_internalFlash.h"
#include "upgrade.h"
#include "upg_apply.h"
#include "flash_manage.h"
#include <string.h>

/* forward decl for debug instrumentation */
uint16_t Cal_CRC16(const uint8_t *data, uint32_t size);

#define YMODEM_MAX_FILE  (2u * 1024u * 1024u)

/* dtb 保留区(SPI flash, 与 platform/hal/dts.h 约定一致):
 *   主槽 0xF00000 / 备份槽 0xF01000, 各 4KB */
#define DTS_MAGIC          0xD45B0001u
#define DTS_FLASH_PRIMARY  0x00F00000u
#define DTS_FLASH_BACKUP   0x00F01000u
#define DTS_SLOT_MAX       4096u

extern STORAGE_HW_OPS_T spi_flash_ops;

uint8_t file_name[FILE_NAME_LENGTH];
uint32_t FlashDestination = ApplicationAddress;
extern uint8_t tab_1024[1024];
YMODEM_FILE_CTRL_T g_stYmodemFileCtrl;
void DelayMs(uint32_t ms);

static uint8_t s_ym_upg;
static uint8_t s_ym_dtb;
static uint8_t s_ym_decided;

/**
  * @brief  按真实地址范围擦除 APP 区域覆盖到的扇区
  */
static void ymodem_erase_app_range(uint32_t start_addr, uint32_t size)
{
    uint32_t addr;
    uint32_t end_addr;

    if (size == 0)
    {
        return;
    }

    end_addr = start_addr + size;
    addr = start_addr;
    while (addr < end_addr)
    {
        internal_flash_erase(addr);
        addr = GetNextSectorAddr(addr);
        /* 防止异常死循环 */
        if (addr <= start_addr)
        {
            break;
        }
    }
}

/**
  * @brief   从发送端接收一个字节
  * @param  c: 接收字符
  *         timeout: 超时时间
  * @retval 0：成功接收
  *         1：时间超时
  */
static void dwt_cyccnt_enable(void)
{
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk))
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    }
    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk))
    {
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

static int32_t Receive_Byte(uint8_t *c, uint32_t timeout_ms)
{
    uint32_t start;
    uint32_t wait_cycles;

    dwt_cyccnt_enable();
    start = DWT->CYCCNT;
    wait_cycles = timeout_ms * (SystemCoreClock / 1000u);

    while ((DWT->CYCCNT - start) < wait_cycles)
    {
        if (USART1->SR & USART_FLAG_RXNE)
        {
            *c = (uint8_t)(USART1->DR & 0xFFu);
            return 0;
        }
    }
    return -1;
}

/** 连续收 n 字节；总超时 timeout_ms（非整包每字节 1s） */
static int32_t Receive_Bytes(uint8_t *dst, uint32_t n, uint32_t timeout_ms)
{
    uint32_t start;
    uint32_t wait_cycles;

    dwt_cyccnt_enable();
    start = DWT->CYCCNT;
    wait_cycles = timeout_ms * (SystemCoreClock / 1000u);

    while (n > 0u)
    {
        if ((DWT->CYCCNT - start) >= wait_cycles)
        {
            return -1;
        }
        if (USART1->SR & USART_FLAG_RXNE)
        {
            *dst++ = (uint8_t)(USART1->DR & 0xFFu);
            n--;
        }
    }
    return 0;
}


/**
  * @brief   发送一个字符
  * @param  c: 发送的字符
  * @retval 0：成功发送
  */
static uint32_t Send_Byte(uint8_t c)
{
    uint32_t timeout_ms = 10;
    uint32_t t = timeout_ms * (SystemCoreClock / 1000u);
    uint32_t start;

    dwt_cyccnt_enable();
    start = DWT->CYCCNT;

    while (!(USART1->SR & USART_FLAG_TXE))
    {
        if ((DWT->CYCCNT - start) > t)
        {
            return (uint32_t)-1;
        }
    }
    USART1->DR = c;

    /* 等移位完成，避免紧接着的日志打印插队导致对端漏 ACK */
    start = DWT->CYCCNT;
    while (!(USART1->SR & USART_FLAG_TC))
    {
        if ((DWT->CYCCNT - start) > t)
        {
            return (uint32_t)-2;
        }
    }
    return 0;
}


/**
  * @brief   从发送端接收一个数据包
  * @param  data ：数据指针
  *         length：长度
  *         timeout ：超时时间
  * @retval 0: 正常返回
  *        -1: 超时或者数据包错误
  *         1: 用户取消
  */
static int32_t Receive_Packet(uint8_t *data, int32_t *length, uint32_t timeout)
{
    uint16_t packet_size;
    uint8_t c;
    *length = 0;
    if (Receive_Byte(&c, timeout) != 0)
    {
        //os_debug_header();
        return -1;
    }
    switch (c)
    {
	case STX_8B:
		packet_size = PACKET_8B_SIZE;
	  break;
	case STX_16B:
	  packet_size = PACKET_16B_SIZE;
	  break;
	case STX_32B:
	  packet_size = PACKET_32B_SIZE;
	  break;
	case STX_64B:
	  packet_size = PACKET_64B_SIZE;
	  break;
	case STX_128B:
    case SOH:
      packet_size = PACKET_128B_SIZE;
      break;
	case STX_256B:
	  packet_size = PACKET_256B_SIZE;
	  break;
	case STX_512B:
	  packet_size = PACKET_512B_SIZE;
	  break;
	case STX_1KB:
	case STX://为了兼容超级终端
      packet_size = PACKET_1KB_SIZE;
      break;
	case STX_2KB:
		packet_size = PACKET_2KB_SIZE;
	  break;
    case EOT:
        //os_debug_header();
        return 0;
    case CA:
        //os_debug_header();
        if ((Receive_Byte(&c, timeout) == 0) && (c == CA))
        {
            *length = -1;
            return 0;
        }
        else
        {
            return -1;
        }
    case ABORT1:
    case ABORT2:
        //os_debug_header();
        return 1;
    default:
        //os_debug_header();
        return -1;
    }
    *data = c;
    /* 仅支持到 1K 包，避免 packet_data 栈缓冲溢出 */
    if (packet_size > PACKET_1K_SIZE)
    {
        return -1;
    }
    /* 余下 seq/payload/CRC 整段快收。
     * 注意：空文件名包只有 128B，USB-UART/虚拟机下 SOH 与后续字节常被拆开，
     * 超时过短会误判失败并回 'C'，PC 端表现为「空包ACK超时」。 */
    {
        uint32_t remain = (uint32_t)packet_size + PACKET_OVERHEAD - 1u;
        if (Receive_Bytes(data + 1, remain, 1000u) != 0)
        {
            return -1;
        }
    }
    if (data[PACKET_SEQNO_INDEX] != ((data[PACKET_SEQNO_COMP_INDEX] ^ 0xff) & 0xff))
    {
        return -1;
    }

    {
        uint16_t rx_crc  = ((uint16_t)data[PACKET_HEADER + packet_size] << 8)
                         | data[PACKET_HEADER + packet_size + 1];
        uint16_t cal_crc = Cal_CRC16(data + PACKET_HEADER, packet_size);
        if (rx_crc != cal_crc)
        {
            return -1;
        }
    }

    *length = packet_size;
    return 0;
}


/**
  * @brief   通过 ymodem协议接收一个文件并写入 APP1 Flash
  * @param  buf: 包暂存缓冲（通常 &tab_1024[0]）
  * @retval >0 文件长度；-1 过大；-2 编程失败；-3 用户取消；0 失败/中止
  */
int32_t Ymodem_Receive(uint8_t *buf)
{
    uint8_t packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD], file_size[FILE_SIZE_LENGTH], *file_ptr;
    int32_t i, packet_length, session_done, file_done, packets_received, errors, session_begin, size = 0;
    uint32_t bytes_written = 0;
    uint32_t write_len = 0;

    (void)buf;

    FlashDestination = ApplicationAddress;
    g_stYmodemFileCtrl.dwYmodemFileRecvedLen = 0;
    s_ym_upg = 0;
    s_ym_dtb = 0;
    s_ym_decided = 0;
    upg_apply_abort();
    Send_Byte(CRC16);

    for (session_done = 0, errors = 0, session_begin = 0;;)
    {
        for (packets_received = 0, file_done = 0;;)
        {
            switch (Receive_Packet(packet_data, &packet_length, NAK_TIMEOUT))
            {
            case 0:
                errors = 0;
                switch (packet_length)
                {
                case -1:
                    /* 发送端 CA CA 中止 */
                    Send_Byte(ACK);
                    upg_apply_abort();
                    return 0;
                case 0:
                    /* EOT：ACK 后再发 'C'，请求空文件名包结束会话 */
                    Send_Byte(ACK);
                    Send_Byte(CRC16);
                    file_done = 1;
                    break;
                default:
                    if ((packet_data[PACKET_SEQNO_INDEX] & 0xff) != (packets_received & 0xff))
                    {
                        Send_Byte(NAK);
                    }
                    else
                    {
                        if (packets_received == 0)
                        {
                            /* 文件名包 */
                            if (packet_data[PACKET_HEADER] != 0)
                            {
                                for (i = 0, file_ptr = packet_data + PACKET_HEADER;
                                     (*file_ptr != 0) && (i < FILE_NAME_LENGTH - 1);)
                                {
                                    file_name[i++] = *file_ptr++;
                                }
                                file_name[i] = '\0';

                                for (i = 0, file_ptr++;
                                     (*file_ptr != ' ') && (*file_ptr != '\0') && (i < FILE_SIZE_LENGTH - 1);)
                                {
                                    file_size[i++] = *file_ptr++;
                                }
                                file_size[i] = '\0';
                                Str2Int(file_size, &size);

                                if ((size <= 0) || ((uint32_t)size > YMODEM_MAX_FILE))
                                {
                                    Send_Byte(CA);
                                    Send_Byte(CA);
                                    return -1;
                                }

                                /* 擦除推迟到首个数据包：upg.bin 与裸 app.bin 走不同介质 */
                                FlashDestination = ApplicationAddress;
                                bytes_written = 0;

                                Send_Byte(ACK);
                                Send_Byte(CRC16);
                            }
                            else
                            {
                                /* 空文件名包：会话结束 */
                                Send_Byte(ACK);
                                file_done = 1;
                                session_done = 1;
                                break;
                            }
                        }
                        else
                        {
                            /* 数据包：只写入尚未写完的有效长度（去掉末包 0x1A 填充） */
                            if ((uint32_t)size > bytes_written)
                            {
                                write_len = (uint32_t)packet_length;
                                if (write_len > ((uint32_t)size - bytes_written))
                                {
                                    write_len = (uint32_t)size - bytes_written;
                                }

                                if (!s_ym_decided)
                                {
                                    uint32_t magic = 0;

                                    memcpy(&magic, packet_data + PACKET_HEADER, sizeof(magic));
                                    if (magic == UPG_HDR_MAGIC_LDR) {
                                        s_ym_upg = 1;
                                        if (upg_apply_begin((uint32_t)size) != 0) {
                                            Send_Byte(CA);
                                            Send_Byte(CA);
                                            return -1;
                                        }
                                    } else if (magic == DTS_MAGIC) {
                                        /* 板级设备树 dtb: 擦除主/备槽, 逐包写入两个槽 */
                                        s_ym_dtb = 1;
                                        if ((uint32_t)size <= 0 || (uint32_t)size > DTS_SLOT_MAX) {
                                            Send_Byte(CA);
                                            Send_Byte(CA);
                                            return -1;
                                        }
                                        spi_flash_ops.hw_erase(DTS_FLASH_PRIMARY);
                                        spi_flash_ops.hw_erase(DTS_FLASH_BACKUP);
                                    } else {
                                        ymodem_erase_app_range(ApplicationAddress, (uint32_t)size);
                                        FlashDestination = ApplicationAddress;
                                    }
                                    s_ym_decided = 1;
                                }

                                if (s_ym_upg)
                                {
                                    if (upg_apply_feed(packet_data + PACKET_HEADER, write_len) != 0) {
                                        upg_apply_abort();
                                        Send_Byte(CA);
                                        Send_Byte(CA);
                                        return -2;
                                    }
                                }
                                else if (s_ym_dtb)
                                {
                                    spi_flash_ops.hw_write(DTS_FLASH_PRIMARY + bytes_written,
                                                           packet_data + PACKET_HEADER,
                                                           write_len);
                                    spi_flash_ops.hw_write(DTS_FLASH_BACKUP + bytes_written,
                                                           packet_data + PACKET_HEADER,
                                                           write_len);
                                }
                                else
                                {
                                    internal_flash_write(FlashDestination,
                                                         packet_data + PACKET_HEADER,
                                                         write_len);
                                    FlashDestination += write_len;
                                }
                                bytes_written += write_len;
                                g_stYmodemFileCtrl.dwYmodemFileRecvedLen = bytes_written;
                            }
                            Send_Byte(ACK);
                        }
                        packets_received++;
                        session_begin = 1;
                    }
                    break;
                }
                break;
            case 1:
                Send_Byte(CA);
                Send_Byte(CA);
                upg_apply_abort();
                return -3;
            default:
                if (session_begin > 0)
                {
                    errors++;
                }
                if (errors > MAX_ERRORS)
                {
                    Send_Byte(CA);
                    Send_Byte(CA);
                    upg_apply_abort();
                    return 0;
                }
                Send_Byte(CRC16);
                break;
            }
            if (file_done != 0)
            {
                break;
            }
        }
        if (session_done != 0)
        {
            break;
        }
    }

    if ((size > 0) && (bytes_written >= (uint32_t)size))
    {
        if (s_ym_upg) {
            if (upg_apply_finish() != 0) {
                upg_apply_abort();
                return -2;
            }
        } else if (s_ym_dtb) {
            SerialPutString("dtb written to primary+backup slots (0xF00000/0xF01000)\r\n");
        }
        return (int32_t)size;
    }
    /* 未完整写入则视为失败，避免误设 APP 地址 */
    return 0;
}

uint8_t Ymodem_LastIsDtb(void)
{
    return s_ym_dtb;
}
/**
  * @brief   通过 ymodem协议检测响应
  * @param  c: 测试字符
  * @retval none
  */
int32_t Ymodem_CheckResponse(uint8_t c)
{
    return 0;
}
/**
  * @brief   准备第一个数据包
  * @param  data：数据包
  *         fileName ：文件名
  *         length ：长度
  * @retval none
  */
void Ymodem_PrepareIntialPacket(uint8_t *data, const uint8_t *fileName, uint32_t *length)
{
    uint16_t i, j;
    uint8_t file_ptr[10];

    //制作头3个数据包
    data[0] = SOH;
    data[1] = 0x00;
    data[2] = 0xff;
    //文件名数据包有效数据
    for (i = 0; (fileName[i] != '\0') && (i < FILE_NAME_LENGTH); i++)
    {
        data[i + PACKET_HEADER] = fileName[i];
    }

    data[i + PACKET_HEADER] = 0x00;

    Int2Str(file_ptr, *length);
    for (j = 0, i = i + PACKET_HEADER + 1; file_ptr[j] != '\0';)
    {
        data[i++] = file_ptr[j++];
    }

    for (j = i; j < PACKET_SIZE + PACKET_HEADER; j++)
    {
        data[j] = 0;
    }
}
/**
  * @brief   准备数据包
  * @param  SourceBuf：数据源缓冲
  *         data：数据包
  *         pktNo ：数据包编号
  *         sizeBlk ：长度
  * @retval none
  */
void Ymodem_PreparePacket(uint8_t *SourceBuf, uint8_t *data, uint8_t pktNo, uint32_t sizeBlk)
{
    uint16_t i, size, packetSize;
    uint8_t *file_ptr;

    //制作头3个数据包
    packetSize = sizeBlk >= PACKET_1K_SIZE ? PACKET_1K_SIZE : PACKET_SIZE;
    size = sizeBlk < packetSize ? sizeBlk : packetSize;
    if (packetSize == PACKET_1K_SIZE)
    {
        data[0] = STX;
    }
    else
    {
        data[0] = SOH;
    }
    data[1] = pktNo;
    data[2] = (~pktNo);
    file_ptr = SourceBuf;

    //文件名数据包有效数据
    for (i = PACKET_HEADER; i < size + PACKET_HEADER; i++)
    {
        data[i] = *file_ptr++;
    }
    if (size <= packetSize)
    {
        for (i = size + PACKET_HEADER; i < packetSize + PACKET_HEADER; i++)
        {
            data[i] = 0x1A; //结束
        }
    }
}
/**
  * @brief   更新输入数据的ＣＲＣ校验
  * @param  crcIn：
  *         byte：
  * @retval ＣＲＣ校验值
  */
uint16_t UpdateCRC16(uint16_t crcIn, uint8_t byte)
{
    uint32_t crc = crcIn;
    uint32_t in = byte | 0x100;
    do
    {
        crc <<= 1;
        in <<= 1;
        if (in & 0x100)
            ++crc;
        if (crc & 0x10000)
            crc ^= 0x1021;
    } while (!(in & 0x10000));
    return crc & 0xffffu;
}
/**
  * @brief   更新输入数据的ＣＲＣ校验
  * @param  data ：数据
  *         size ：长度
  * @retval ＣＲＣ校验值
  */
#if 0 
uint16_t Cal_CRC16(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0;
    const uint8_t *dataEnd = data + size;
    while (data < dataEnd)
        crc = UpdateCRC16(crc, *data++);

    //crc = UpdateCRC16(crc, 0);
    //crc = UpdateCRC16(crc, 0);
    return crc & 0xffffu;
}
#endif
uint16_t Cal_CRC16(const uint8_t *data, uint32_t size)
{
    uint16_t crc = 0;
    uint32_t i;
    for (i = 0; i < size; i++) {
        int j;
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (uint16_t)(crc << 1) ^ 0x1021;
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}


/**
  * @brief  计算YModem数据包的总大小
  * @param  data ：数据
  *         size ：长度
  * @retval 数据包的总大小
  */
uint8_t CalChecksum(const uint8_t *data, uint32_t size)
{
    uint32_t sum = 0;
    const uint8_t *dataEnd = data + size;
    while (data < dataEnd)
        sum += *data++;
    return sum & 0xffu;
}
/**
  * @brief  通过ymodem协议传输一个数据包
  * @param  data ：数据地址指针
  *         length：长度
  * @retval none
  */
void Ymodem_SendPacket(uint8_t *data, uint16_t length)
{
    uint16_t i;
    i = 0;
    while (i < length)
    {
        Send_Byte(data[i]);
        i++;
    }
}
/**
  * @brief  通过ymodem协议传输一个文件
  * @param  buf ：数据地址指针
  *         sendFileName ：文件名
  *         sizeFile：文件长度
  * @retval 0：成功
  */
uint8_t Ymodem_Transmit(uint8_t *buf, const uint8_t *sendFileName, uint32_t sizeFile)
{

    uint8_t packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD];
    uint8_t FileName[FILE_NAME_LENGTH];
    uint8_t *buf_ptr, tempCheckSum;
    uint16_t tempCRC, blkNumber;
    uint8_t receivedC[2], CRC16_F = 0, i;
    uint32_t errors, ackReceived, size = 0, pktSize;

    errors = 0;
    ackReceived = 0;
    for (i = 0; i < (FILE_NAME_LENGTH - 1); i++)
    {
        FileName[i] = sendFileName[i];
    }
    CRC16_F = 1;

    //准备第一个数据包
    Ymodem_PrepareIntialPacket(&packet_data[0], FileName, &sizeFile);

    do
    {
        //发送数据包
        Ymodem_SendPacket(packet_data, PACKET_SIZE + PACKET_HEADER);
        //发送CRC校验
        if (CRC16_F)
        {
            tempCRC = Cal_CRC16(&packet_data[3], PACKET_SIZE);
            Send_Byte(tempCRC >> 8);
            Send_Byte(tempCRC & 0xFF);
        }
        else
        {
            tempCheckSum = CalChecksum(&packet_data[3], PACKET_SIZE);
            Send_Byte(tempCheckSum);
        }

        //等待响应
        if (Receive_Byte(&receivedC[0], 10000) == 0)
        {
            if (receivedC[0] == ACK)
            {
                //数据包正确传输
                ackReceived = 1;
            }
        }
        else
        {
            errors++;
        }
    } while (!ackReceived && (errors < 0x0A));

    if (errors >= 0x0A)
    {
        return errors;
    }
    buf_ptr = buf;
    size = sizeFile;
    blkNumber = 0x01;

    //1024字节的数据包发送
    while (size)
    {
        //准备下一个数据包
        Ymodem_PreparePacket(buf_ptr, &packet_data[0], blkNumber, size);
        ackReceived = 0;
        receivedC[0] = 0;
        errors = 0;
        do
        {
            //发送下一个数据包
            if (size >= PACKET_1K_SIZE)
            {
                pktSize = PACKET_1K_SIZE;
            }
            else
            {
                pktSize = PACKET_SIZE;
            }
            Ymodem_SendPacket(packet_data, pktSize + PACKET_HEADER);
            //发送CRC校验
            if (CRC16_F)
            {
                tempCRC = Cal_CRC16(&packet_data[3], pktSize);
                Send_Byte(tempCRC >> 8);
                Send_Byte(tempCRC & 0xFF);
            }
            else
            {
                tempCheckSum = CalChecksum(&packet_data[3], pktSize);
                Send_Byte(tempCheckSum);
            }

            //等待响应
            if ((Receive_Byte(&receivedC[0], 100000) == 0) && (receivedC[0] == ACK))
            {
                ackReceived = 1;
                if (size > pktSize)
                {
                    buf_ptr += pktSize;
                    size -= pktSize;
                    if (blkNumber == (FLASH_IMAGE_SIZE / 1024))
                    {
                        return 0xFF; //错误
                    }
                    else
                    {
                        blkNumber++;
                    }
                }
                else
                {
                    buf_ptr += pktSize;
                    size = 0;
                }
            }
            else
            {
                errors++;
            }
        } while (!ackReceived && (errors < 0x0A));
        //如果没响应10次就返回错误
        if (errors >= 0x0A)
        {
            return errors;
        }
    }
    ackReceived = 0;
    receivedC[0] = 0x00;
    errors = 0;
    do
    {
        Send_Byte(EOT);
        //发送 (EOT);
        //等待回应
        if ((Receive_Byte(&receivedC[0], 10000) == 0) && receivedC[0] == ACK)
        {
            ackReceived = 1;
        }
        else
        {
            errors++;
        }
    } while (!ackReceived && (errors < 0x0A));

    if (errors >= 0x0A)
    {
        return errors;
    }
    //准备最后一个包
    ackReceived = 0;
    receivedC[0] = 0x00;
    errors = 0;

    packet_data[0] = SOH;
    packet_data[1] = 0;
    packet_data[2] = 0xFF;

    for (i = PACKET_HEADER; i < (PACKET_SIZE + PACKET_HEADER); i++)
    {
        packet_data[i] = 0x00;
    }

    do
    {
        //发送数据包
        Ymodem_SendPacket(packet_data, PACKET_SIZE + PACKET_HEADER);
        //发送CRC校验
        tempCRC = Cal_CRC16(&packet_data[3], PACKET_SIZE);
        Send_Byte(tempCRC >> 8);
        Send_Byte(tempCRC & 0xFF);

        //等待响应
        if (Receive_Byte(&receivedC[0], 10000) == 0)
        {
            if (receivedC[0] == ACK)
            {
                //包传输正确
                ackReceived = 1;
            }
        }
        else
        {
            errors++;
        }

    } while (!ackReceived && (errors < 0x0A));
    //如果没响应10次就返回错误
    if (errors >= 0x0A)
    {
        return errors;
    }

    do
    {
        Send_Byte(EOT);
        //发送 (EOT);
        //等待回应
        if ((Receive_Byte(&receivedC[0], 10000) == 0) && receivedC[0] == ACK)
        {
            ackReceived = 1;
        }
        else
        {
            errors++;
        }
    } while (!ackReceived && (errors < 0x0A));

    if (errors >= 0x0A)
    {
        return errors;
    }
    return 0; //文件传输成功
}


