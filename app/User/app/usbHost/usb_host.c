#include <string.h>
#include "usb_host.h"
#include "os_task.h"
#include "os_debug.h"
#include "os_mutex.h"
#include "hal_flash.h"

#include "usb_bsp.h"
#include "usb_conf.h"
#include "usbh_core.h"
#include "usbh_msc_core.h"
#include "usbh_msc_scsi.h"
#include "usbh_msc_bot.h"
#include "usbh_usr.h"
#include "usb_hcd.h"

#include "ff.h"

/* USB host 全局实例（由 usbh_usr.c 定义） */
extern USB_OTG_CORE_HANDLE USB_OTG_Core;
extern USBH_HOST           USB_Host;
extern USBH_Usr_cb_TypeDef USR_cb;
extern USBH_Class_cb_TypeDef USBH_MSC_cb;

/* U 盘作为 FatFs 物理卷 pdrv=2，逻辑卷名 "2:" */
#define USB_MSC_PDRV       2
#define USB_MSC_VOLUME     "2:"

static FATFS s_usb_fs;
static volatile uint8_t s_mounted = 0;

static void usb_mount(void)
{
    FRESULT res;

    if (s_mounted) {
        return;
    }
    res = f_mount(&s_usb_fs, USB_MSC_VOLUME, 1);
    if (res == FR_OK) {
        s_mounted = 1;
        os_printf("usb_host: U-disk mounted on %s\r\n", USB_MSC_VOLUME);
    } else {
        os_printf("usb_host: mount %s fail res=%d\r\n", USB_MSC_VOLUME, (int)res);
    }
}

static void usb_unmount(void)
{
    if (!s_mounted) {
        return;
    }
    f_mount(NULL, USB_MSC_VOLUME, 0);
    s_mounted = 0;
    os_printf("usb_host: U-disk unmounted\r\n");
}

void usb_host_init(void)
{
    USBH_Init(&USB_OTG_Core,
              USB_OTG_FS_CORE_ID,
              &USB_Host,
              &USBH_MSC_cb,
              &USR_cb);
    os_printf("usb_host: USBH_Init done, waiting U-disk...\r\n");
}

void usb_host_task(void *arg)
{
    (void)arg;
    for (;;) {
        USBH_Process(&USB_OTG_Core, &USB_Host);

        if (HCD_IsDeviceConnected(&USB_OTG_Core)) {
            /* MSC GET_CAPACITY 成功后 MSCapacity 非零，枚举进入 class 阶段才挂载 */
            if (USBH_MSC_Param.MSCapacity != 0) {
                usb_mount();
            }
        } else {
            usb_unmount();
        }

        os_sleep_ms(20);
    }
}
