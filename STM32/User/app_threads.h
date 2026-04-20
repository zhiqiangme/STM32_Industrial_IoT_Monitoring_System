#ifndef __APP_THREADS_H
#define __APP_THREADS_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppThreads_HardwareInit(void);
rt_err_t AppThreads_Create(void);

#ifdef __cplusplus
}
#endif

#endif
