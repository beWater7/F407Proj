#include "hal_flash.h"
#include "bsp_spi_flash.h"
#include "bsp_internalFlash.h"

void hal_spi_flash_init(void)
{
    SPI_FLASH_Init();
}

void hal_spi_flash_wakeup(void)
{
    SPI_Flash_WAKEUP();
}

uint32_t hal_spi_flash_read_id(void)
{
    return SPI_FLASH_ReadID();
}

void hal_spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    SPI_FLASH_BufferRead(addr, buf, len);
}

void hal_spi_flash_write(uint32_t addr, uint8_t *buf, uint32_t len)
{
    SPI_FLASH_BufferWrite(addr, buf, len);
}

void hal_spi_flash_erase_sector(uint32_t addr)
{
    SPI_FLASH_SectorErase(addr);
}

void hal_int_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    internal_flash_read(addr, buf, len);
}

void hal_int_flash_write(uint32_t addr, uint8_t *buf, uint32_t len)
{
    internal_flash_write(addr, buf, len);
}

void hal_int_flash_erase(uint32_t addr)
{
    internal_flash_erase(addr);
}

uint32_t hal_int_flash_get_sector(uint32_t addr)
{
    return GetSector(addr);
}

int hal_int_flash_erase_range(uint32_t start_addr, uint32_t end_addr)
{
    uint32_t sector = GetSector(start_addr);
    uint32_t last = GetSector(end_addr);
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    while (sector < last) {
        if (FLASH_EraseSector(sector, VoltageRange_3) != FLASH_COMPLETE) {
            FLASH_Lock();
            return -1;
        }
        sector += 8;
    }
    FLASH_Lock();
    return 0;
}
