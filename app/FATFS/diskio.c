/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2014        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "diskio.h"		/* FatFs lower layer API */
#include "ff.h"
#include "bsp_spi_flash.h"

/* ?????õô?????????????? */
#define ATA			    0     // ???SD?????
#define SPI_FLASH		1     // ??SPI Flash

/*-----------------------------------------------------------------------*/
/* ????õô??                                                          */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status (
	BYTE pdrv		/* ??????? */
)
{

	DSTATUS status = STA_NOINIT;
	
	switch (pdrv) {
		case ATA:	/* SD CARD */
			break;
    
		case SPI_FLASH:      
      /* SPI Flash????????SPI Flash ?õôID */
      if(sFLASH_ID == SPI_FLASH_ReadID())
      {
        /* ?õôID????????? */
        status &= ~STA_NOINIT;
      }
      else
      {
        /* ?õôID?????????? */
        status = STA_NOINIT;;
      }
			break;

		default:
			status = STA_NOINIT;
	}
	return status;
}

/*-----------------------------------------------------------------------*/
/* ?õô?????                                                            */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize (
	BYTE pdrv				/* ??????? */
)
{
  uint16_t i;
	DSTATUS status = STA_NOINIT;	
	switch (pdrv) {
		case ATA:	         /* SD CARD */
			break;
    
		case SPI_FLASH:    /* SPI Flash */ 
            /* ?????SPI Flash */
			SPI_FLASH_Init();
            /* ????§³????? */
            i=500;
	        while(--i);	
            /* ????SPI Flash */
	        SPI_Flash_WAKEUP();
            /* ???SPI Flash§à??? */
            status=disk_status(SPI_FLASH);
			break;
		default:
			status = STA_NOINIT;
	}
	return status;
}


/*-----------------------------------------------------------------------*/
/* ????????????????????????›¥??                                              */
/*-----------------------------------------------------------------------*/
DRESULT disk_read (
	BYTE pdrv,		/* ?õô???????(0..) */
	BYTE *buff,		/* ????????? */
	DWORD sector,	/* ???????? */
	UINT count		/* ????????(1..128) */
)
{
	DRESULT status = RES_PARERR;
	switch (pdrv) {
		case ATA:	/* SD CARD */
			break;
    
		case SPI_FLASH:
      /* ???????6MB????Flash???????????SPI Flash????10MB??? */
      sector+=1536; //6 * 256 == 1536
//      /* ??????Fatfs??§Õ???????????????????????? */
//      if(sector > 3072) // 6(?????)+6(Fatfs???) * 256
//      {
//         status = RES_PARERR;
//         break;
//      }
      SPI_FLASH_BufferRead(sector <<12, buff,  count<<12);
      status = RES_OK;
		break;
    
		default:
			status = RES_PARERR;
	}
	return status;
}

/*-----------------------------------------------------------------------*/
/* §Õ????????????§Õ??????????????                                      */
/*-----------------------------------------------------------------------*/
#if _USE_WRITE
DRESULT disk_write (
	BYTE pdrv,			  /* ?õô???????(0..) */
	const BYTE *buff,	/* ??§Õ???????????? */
	DWORD sector,		  /* ???????? */
	UINT count			  /* ????????(1..128) */
)
{
    uint32_t write_addr;
	DRESULT status = RES_PARERR;
	if (!count) {
		return RES_PARERR;		/* Check parameter */
	}

	switch (pdrv) {
		case ATA:	/* SD CARD */      
		break;

		case SPI_FLASH:
        /* ???????6MB????Flash???????????SPI Flash????10MB??? */
		sector+=1536;
        /* ??????Fatfs??§Õ???????????????????????? */
//        if(sector > 3072) // 6(?????)+6(Fatfs???) * 256
//        {
//            status = RES_PARERR;
//            break;
//        }
        write_addr = sector<<12;
        SPI_FLASH_SectorErase(write_addr);
        SPI_FLASH_BufferWrite(write_addr, (u8 *)buff, count<<12);
        status = RES_OK;
        break;

		default:
			status = RES_PARERR;
	}
	return status;
}
#endif


/*-----------------------------------------------------------------------*/
/* ????????                                                              */
/*-----------------------------------------------------------------------*/

#if _USE_IOCTL
DRESULT disk_ioctl (
	BYTE pdrv,		/* ??????? */
	BYTE cmd,		  /* ??????? */
	void *buff		/* §Õ???????????????? */
)
{
	DRESULT status = RES_PARERR;
	switch (pdrv) {
		case ATA:	/* SD CARD */
			break;
    
		case SPI_FLASH:
			switch (cmd) {
        /* ??????????2560*4096/1024/1024=10(MB) */
        case GET_SECTOR_COUNT:
          *(DWORD * )buff = 2560;		
        break;
        /* ??????§³  */
        case GET_SECTOR_SIZE :
          *(WORD * )buff = 4096;
        break;
        /* ?????????????? */
        case GET_BLOCK_SIZE :
          *(DWORD * )buff = 1;
        break;        
      }
      status = RES_OK;
		break;
    
		default:
			status = RES_PARERR;
	}
	return status;
}
#endif

__attribute__((weak)) DWORD get_fattime(void) {
	/* ·µ»Øµ±Ç°Ê±¼ä´Á */
	return	  ((DWORD)(2015 - 1980) << 25)	/* Year 2015 */
			| ((DWORD)1 << 21)				/* Month 1 */
			| ((DWORD)1 << 16)				/* Mday 1 */
			| ((DWORD)0 << 11)				/* Hour 0 */
			| ((DWORD)0 << 5)				  /* Min 0 */
			| ((DWORD)0 >> 1);				/* Sec 0 */
}



