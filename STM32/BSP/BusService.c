#include "BusService.h"

/* USART2 主站总线互斥体：所有现场设备访问都从这里串行化。 */
static struct rt_mutex s_modbus_master_mutex;
static rt_bool_t s_bus_ready = RT_FALSE;

rt_err_t BusService_Init(void)
{
    if (s_bus_ready != RT_FALSE)
    {
        return RT_EOK;
    }

    if (rt_mutex_init(&s_modbus_master_mutex, "mbus", RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        return -RT_ERROR;
    }

    s_bus_ready = RT_TRUE;
    return RT_EOK;
}

rt_err_t BusService_Lock(rt_int32_t timeout)
{
    if (s_bus_ready == RT_FALSE)
    {
        return -RT_ERROR;
    }

    /* 由调用方围住一次完整的 Modbus 请求/响应事务。 */
    return rt_mutex_take(&s_modbus_master_mutex, timeout);
}

void BusService_Unlock(void)
{
    if (s_bus_ready != RT_FALSE)
    {
        (void)rt_mutex_release(&s_modbus_master_mutex);
    }
}
