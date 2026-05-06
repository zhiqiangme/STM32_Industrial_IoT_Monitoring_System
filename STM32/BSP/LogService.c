#include "LogService.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOGSERVICE_ERROR_TEXT_MAX      96u
#define LOGSERVICE_QUEUE_DEPTH         16u
#define LOGSERVICE_FLUSH_PERIOD_MS     60000u

/* logger 线程消费的消息类型：采样数据、错误日志、强制刷盘。 */
typedef enum
{
    LOGSERVICE_MSG_SENSOR = 0,
    LOGSERVICE_MSG_ERROR = 1,
    LOGSERVICE_MSG_FLUSH = 2,
} LogServiceMessageType;

/* 队列里统一传输的小消息包，避免业务线程直接访问 LittleFS。 */
typedef struct
{
    rt_uint8_t type;
    rt_uint8_t reserved[3];
    union
    {
        SensorSnapshot sensor;
        char error_text[LOGSERVICE_ERROR_TEXT_MAX];
    } payload;
} LogServiceMessage;

#define LOGSERVICE_MSG_SLOT_SIZE \
    (RT_ALIGN(sizeof(LogServiceMessage), RT_ALIGN_SIZE) + sizeof(rt_ubase_t))

static struct rt_messagequeue s_log_queue;
static rt_uint8_t s_log_queue_pool[LOGSERVICE_MSG_SLOT_SIZE * LOGSERVICE_QUEUE_DEPTH];
static rt_bool_t s_log_ready = RT_FALSE;
static rt_uint32_t s_log_drop_count = 0u;

static rt_err_t LogService_SubmitMessage(const LogServiceMessage *msg, rt_int32_t timeout)
{
    rt_err_t err;

    if (s_log_ready == RT_FALSE || msg == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (timeout == RT_WAITING_NO)
    {
        err = rt_mq_send(&s_log_queue, msg, sizeof(*msg));
    }
    else
    {
        err = rt_mq_send_wait(&s_log_queue, msg, sizeof(*msg), timeout);
    }

    if (err != RT_EOK)
    {
        s_log_drop_count++;
    }

    return err;
}

rt_err_t LogService_Init(void)
{
    if (s_log_ready != RT_FALSE)
    {
        return RT_EOK;
    }

    if (rt_mq_init(&s_log_queue,
                   "logmq",
                   s_log_queue_pool,
                   sizeof(LogServiceMessage),
                   sizeof(s_log_queue_pool),
                   RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        return -RT_ERROR;
    }

    s_log_ready = RT_TRUE;
    s_log_drop_count = 0u;
    return RT_EOK;
}

rt_err_t LogService_SubmitSensor(const SensorSnapshot *snap)
{
    LogServiceMessage msg;

    if (snap == RT_NULL)
    {
        return -RT_ERROR;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = LOGSERVICE_MSG_SENSOR;
    msg.payload.sensor = *snap;
    /* 采样数据允许在队列满时直接丢弃，避免反压高优先级线程。 */
    return LogService_SubmitMessage(&msg, RT_WAITING_NO);
}

rt_err_t LogService_SubmitErrorf(const char *fmt, ...)
{
    LogServiceMessage msg;
    va_list args;
    int len;

    if (fmt == RT_NULL)
    {
        return -RT_ERROR;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = LOGSERVICE_MSG_ERROR;

    va_start(args, fmt);
    len = vsnprintf(msg.payload.error_text, sizeof(msg.payload.error_text), fmt, args);
    va_end(args);

    if (len < 0)
    {
        return -RT_ERROR;
    }

    msg.payload.error_text[sizeof(msg.payload.error_text) - 1u] = '\0';
    /* 异常信息除了落盘，也直接回显到串口1，便于现场快速判断。 */
    printf("[ERR] %s\r\n", msg.payload.error_text);
    return LogService_SubmitMessage(&msg, rt_tick_from_millisecond(50));
}

rt_err_t LogService_SubmitFlush(void)
{
    LogServiceMessage msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = LOGSERVICE_MSG_FLUSH;
    return LogService_SubmitMessage(&msg, rt_tick_from_millisecond(50));
}

void LogService_ThreadEntry(void *parameter)
{
    rt_bool_t logger_ready = RT_FALSE;
    uint32_t last_flush_tick = HAL_GetTick();

    RT_UNUSED(parameter);

    if (DataLogger_Init() == 0)
    {
        logger_ready = RT_TRUE;
    }
    else
    {
        printf("[LOG] DataLogger init FAILED\r\n");
    }

    while (1)
    {
        LogServiceMessage msg;
        rt_err_t recv_err;
        uint32_t now;

        /* 最多阻塞 1 秒，既等消息也定期检查刷盘周期。 */
        recv_err = rt_mq_recv(&s_log_queue,
                              &msg,
                              sizeof(msg),
                              rt_tick_from_millisecond(1000));

        if (recv_err == RT_EOK && logger_ready != RT_FALSE)
        {
            switch ((LogServiceMessageType)msg.type)
            {
                case LOGSERVICE_MSG_SENSOR:
                    (void)DataLogger_LogSensor(&msg.payload.sensor);
                    break;

                case LOGSERVICE_MSG_ERROR:
                    (void)DataLogger_LogError("%s", msg.payload.error_text);
                    break;

                case LOGSERVICE_MSG_FLUSH:
                    (void)DataLogger_Flush();
                    last_flush_tick = HAL_GetTick();
                    break;

                default:
                    break;
            }
        }

        now = HAL_GetTick();
        /* 即使没有新消息，也保留 60 秒一次的定期刷盘。 */
        if (logger_ready != RT_FALSE && (now - last_flush_tick) >= LOGSERVICE_FLUSH_PERIOD_MS)
        {
            (void)DataLogger_Flush();
            last_flush_tick = now;
        }
    }
}
