#ifndef _HAL_FLASH_H
#define _HAL_FLASH_H

#include <stdint.h>

#ifndef HAL_SPI_FLASH_PAGE_SIZE
#define HAL_SPI_FLASH_PAGE_SIZE 256u
#endif

#ifndef HAL_SPI_FLASH_JEDEC_W25Q128
#define HAL_SPI_FLASH_JEDEC_W25Q128 0xEF4018u
#endif

#ifdef __cplusplus
extern "C" {
#endif

void hal_spi_flash_init(void);
void hal_spi_flash_wakeup(void);
uint32_t hal_spi_flash_read_id(void);
void hal_spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
void hal_spi_flash_write(uint32_t addr, uint8_t *buf, uint32_t len);
void hal_spi_flash_erase_sector(uint32_t addr);

void hal_int_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
void hal_int_flash_write(uint32_t addr, uint8_t *buf, uint32_t len);
void hal_int_flash_erase(uint32_t addr);
int hal_int_flash_erase_range(uint32_t start_addr, uint32_t end_addr);
uint32_t hal_int_flash_get_sector(uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif /* _HAL_FLASH_H */
