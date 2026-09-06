#ifndef __UPG_APPLY_H__
#define __UPG_APPLY_H__

#include <stdint.h>

int upg_apply_begin(uint32_t total_len);
int upg_apply_feed(const uint8_t *data, uint32_t len);
int upg_apply_finish(void);
void upg_apply_abort(void);
int upg_apply_want_reboot(void);

#endif
