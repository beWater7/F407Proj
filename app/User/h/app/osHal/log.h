#ifndef __LOG_H
#define __LOG_H

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_BUF_MAX_SIZE   (4096)
#define LOG_PREFIX_LEN     (32)

void log_init(void);
void writeLog(const char *fmt, ...);
int  readLogAll(char *buf, int max_len);

#ifdef __cplusplus
}
#endif

#endif /* __LOG_H */
