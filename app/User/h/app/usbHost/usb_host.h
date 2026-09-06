#ifndef __APP_USB_HOST_H__
#define __APP_USB_HOST_H__

#ifdef __cplusplus
extern "C" {
#endif

/* USB host (OTG_FS + MSC) 初始化：在 app 启动阶段调用一次 */
void usb_host_init(void);

/* USB host 周期处理任务：轮询 USBH_Process 并在 U 盘枚举后自动挂载 FatFs 卷 "2:" */
void usb_host_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* __APP_USB_HOST_H__ */
