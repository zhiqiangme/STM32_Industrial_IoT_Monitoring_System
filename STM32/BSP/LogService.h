#ifndef __LOGSERVICE_H
#define __LOGSERVICE_H

#include "DataLogger.h"
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t LogService_Init(void);
rt_err_t LogService_SubmitSensor(const SensorSnapshot *snap);
rt_err_t LogService_SubmitErrorf(const char *fmt, ...);
rt_err_t LogService_SubmitFlush(void);
void LogService_ThreadEntry(void *parameter);

#ifdef __cplusplus
}
#endif

#endif
