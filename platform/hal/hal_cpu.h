#ifndef _HAL_CPU_H
#define _HAL_CPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hal_cpu_irq_disable(void);
void hal_cpu_irq_enable(void);
void hal_cpu_reset(void);
uint32_t hal_cpu_get_vtor(void);
uint32_t hal_cpu_get_msp(void);

#ifdef __cplusplus
}
#endif

#endif /* _HAL_CPU_H */
