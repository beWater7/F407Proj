#ifndef _HAL_KEY_H
#define _HAL_KEY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    HAL_KEY_1 = 0,
    HAL_KEY_2,
    HAL_KEY_NUM
} hal_key_id_t;

void hal_key_init(void);

/* 按键按下返回非 0，释放返回 0 */
int hal_key_is_pressed(hal_key_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* _HAL_KEY_H */
