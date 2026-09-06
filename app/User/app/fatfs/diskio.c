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
#include "hal_flash.h"

#if defined(CONFIG_APP_USB_HOST)
#include "usb_conf.h"
#include "usbh_msc_core.h"
#include "usbh_msc_scsi.h"
#include "usb_hcd.h"
extern USB_OTG_CORE_HANDLE USB_OTG_Core;
extern USBH_HOST           USB_Host;
#endif

/* ?????��?????????????? */
#define ATA			    0     // ???SD?????
#define SPI_FLASH		1     // ??SPI Flash
#define USB_MSC		    2     // USB U-Disk (CONFIG_APP_USB_HOST)

/*-----------------------------------------------------------------------*/
/* ????��??                                                          */
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
      /* SPI Flash????????SPI Flash ?��ID */
      if(HAL_SPI_FLASH_JEDEC_W25Q128 == hal_spi_flash_read_id())
      {
        /* ?��ID????????? */
        status &= ~STA_NOINIT;
      }
      else
      {
        /* ?��ID?????????? */
        status = STA_NOINIT;;
      }
			break;

#if defined(CONFIG_APP_USB_HOST)
		case USB_MSC:
			/* USB 盘连接并枚举成功后才认为就绪 */
			if (HCD_IsDeviceConnected(&USB_OTG_Core)) {
				status &= ~STA_NOINIT;
			}
			break;
#endif

		default:
			status = STA_NOINIT;
	}
	return status;
}

/*-----------------------------------------------------------------------*/
/* ?��?????                                                            */
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
			hal_spi_flash_init();
            /* ????��????? */
            i=500;
	        while(--i);	
            /* ????SPI Flash */
	        hal_spi_flash_wakeup();
            /* ???SPI Flash��??? */
            status=disk_status(SPI_FLASH);
			break;
#if defined(CONFIG_APP_USB_HOST)
		case USB_MSC:     /* USB U-Disk */
			status = disk_status(USB_MSC);
			break;
#endif
		default:
			status = STA_NOINIT;
	}
	return status;
}


/*-----------------------------------------------------------------------*/
/* ????????????????????????��??                                              */
/*-----------------------------------------------------------------------*/
DRESULT disk_read (
	BYTE pdrv,		/* ?��???????(0..) */
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
//      /* ??????Fatfs??��???????????????????????? */
//      if(sector > 3072) // 6(?????)+6(Fatfs???) * 256
//      {
//         status = RES_PARERR;
//         break;
//      }
      hal_spi_flash_read(sector <<12, buff,  count<<12);
      status = RES_OK;
		break;
    
#if defined(CONFIG_APP_USB_HOST)
	case USB_MSC:      /* USB U-Disk */
	{
		BYTE xfer_status = USBH_MSC_OK;
		do {
			xfer_status = USBH_MSC_Read10(&USB_OTG_Core, buff, sector, 512 * count);
			USBH_MSC_HandleBOTXfer(&USB_OTG_Core, &USB_Host);
			if (!HCD_IsDeviceConnected(&USB_OTG_Core)) {
				return RES_ERROR;
			}
		} while (xfer_status == USBH_MSC_BUSY);
		status = (xfer_status == USBH_MSC_OK) ? RES_OK : RES_ERROR;
		break;
	}
#endif

		default:
			status = RES_PARERR;
	}
	return status;
}

/*-----------------------------------------------------------------------*/
/* ��????????????��??????????????                                      */
/*-----------------------------------------------------------------------*/
#if _USE_WRITE
DRESULT disk_write (
	BYTE pdrv,			  /* ?��???????(0..) */
	const BYTE *buff,	/* ??��???????????? */
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
        /* ??????Fatfs??��???????????????????????? */
//        if(sector > 3072) // 6(?????)+6(Fatfs???) * 256
//        {
//            status = RES_PARERR;
//            break;
//        }
        write_addr = sector<<12;
        hal_spi_flash_erase_sector(write_addr);
        hal_spi_flash_write(write_addr, (uint8_t *)buff, count<<12);
        status = RES_OK;
        break;

#if defined(CONFIG_APP_USB_HOST)
	case USB_MSC:      /* USB U-Disk */
	{
		BYTE xfer_status = USBH_MSC_OK;
		do {
			xfer_status = USBH_MSC_Write10(&USB_OTG_Core, (BYTE *)buff, sector, 512 * count);
			USBH_MSC_HandleBOTXfer(&USB_OTG_Core, &USB_Host);
			if (!HCD_IsDeviceConnected(&USB_OTG_Core)) {
				return RES_ERROR;
			}
		} while (xfer_status == USBH_MSC_BUSY);
		status = (xfer_status == USBH_MSC_OK) ? RES_OK : RES_ERROR;
		break;
	}
#endif

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
	void *buff		/* ��???????????????? */
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
        /* ??????��  */
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

#if defined(CONFIG_APP_USB_HOST)
	case USB_MSC:      /* USB U-Disk */
		switch (cmd) {
		case CTRL_SYNC:
			status = RES_OK;
			break;
		case GET_SECTOR_COUNT:
			*(DWORD *)buff = (DWORD)USBH_MSC_Param.MSCapacity;
			status = RES_OK;
			break;
		case GET_SECTOR_SIZE:
			*(WORD *)buff = 512;
			status = RES_OK;
			break;
		case GET_BLOCK_SIZE:
			*(DWORD *)buff = 512;
			status = RES_OK;
			break;
		default:
			status = RES_PARERR;
		}
		break;
#endif
    
		default:
			status = RES_PARERR;
	}
	return status;
}
#endif

__attribute__((weak)) DWORD get_fattime(void) {
	/* ���?�??��� */
	return	  ((DWORD)(2015 - 1980) << 25)	/* Year 2015 */
			| ((DWORD)1 << 21)				/* Month 1 */
			| ((DWORD)1 << 16)				/* Mday 1 */
			| ((DWORD)0 << 11)				/* Hour 0 */
			| ((DWORD)0 << 5)				  /* Min 0 */
			| ((DWORD)0 >> 1);				/* Sec 0 */
}



