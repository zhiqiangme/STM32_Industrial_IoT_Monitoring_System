#ifndef __BUSSERVICE_H
#define __BUSSERVICE_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t BusService_Init(void);
rt_err_t BusService_Lock(rt_int32_t timeout);
void BusService_Unlock(void);

#ifdef __cplusplus
}
#endif

#endif
