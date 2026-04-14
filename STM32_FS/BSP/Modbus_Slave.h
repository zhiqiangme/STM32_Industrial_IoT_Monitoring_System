#ifndef __MODBUS_SLAVE_H
#define __MODBUS_SLAVE_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define MODBUS_SLAVE_BAUDRATE         115200
#define MODBUS_SLAVE_RX_BUFFER_SIZE   256
#define MODBUS_SLAVE_TX_BUFFER_SIZE   256
#define MODBUS_SLAVE_FRAME_TIMEOUT_MS 10

#define MODBUS_FC_READ_HOLD_REGS      0x03
#define MODBUS_FC_READ_INPUT_REGS     0x04
#define MODBUS_FC_WRITE_SINGLE_COIL   0x05
#define MODBUS_FC_WRITE_SINGLE_REG    0x06

typedef uint8_t (*ModbusSlaveReadRegsFn)(uint16_t start_addr,
                                         uint16_t reg_count,
                                         uint16_t *out_regs,
                                         void *context);
typedef uint8_t (*ModbusSlaveWriteRegFn)(uint16_t reg_addr,
                                         uint16_t reg_value,
                                         void *context);
typedef uint8_t (*ModbusSlaveWriteCoilFn)(uint16_t coil_addr,
                                          bool coil_value,
                                          void *context);

typedef struct
{
    uint8_t slave_addr;
    void *context;
    ModbusSlaveReadRegsFn read_holding_registers;
    ModbusSlaveReadRegsFn read_input_registers;
    ModbusSlaveWriteRegFn write_single_register;
    ModbusSlaveWriteCoilFn write_single_coil;
} ModbusSlaveConfig;

/**
 * @brief 初始化通用 Modbus RTU 从站引擎，并注册业务层读写回调。
 * @param config: 从站配置，包括从站地址、业务上下文和读写回调
 * @retval 无
 */
void Modbus_Slave_Init(const ModbusSlaveConfig *config);

/**
 * @brief 在主循环中处理 Modbus RTU 从站任务。
 * @param 无
 * @retval 无
 */
void Modbus_Slave_Process(void);

/**
 * @brief USART3 接收中断回调入口，用于缓存收到的单个字节。
 * @param byte: 刚收到的 1 个字节
 * @retval 无
 */
void Modbus_Slave_RxCallback(uint8_t byte);

/**
 * @brief 获取通用从站引擎使用的底层 UART 句柄。
 * @param 无
 * @retval UART_HandleTypeDef*: USART3 句柄指针
 */
UART_HandleTypeDef *Modbus_Slave_GetHandle(void);

/**
 * @brief 通知从站引擎发生了一次 USART overrun 异常。
 * @param 无
 * @retval 无
 */
void Modbus_Slave_NotifyUartOverrun(void);

/**
 * @brief 通知从站引擎发生了一次 USART 帧错误。
 * @param 无
 * @retval 无
 */
void Modbus_Slave_NotifyUartFrameError(void);

/**
 * @brief 通知从站引擎发生了一次 USART 噪声错误。
 * @param 无
 * @retval 无
 */
void Modbus_Slave_NotifyUartNoiseError(void);

/**
 * @brief 获取 Modbus CRC 错误累计次数。
 * @param 无
 * @retval uint32_t: CRC 错误次数
 */
uint32_t Modbus_Slave_GetCrcErrorCount(void);

/**
 * @brief 获取 UART 异常累计次数。
 * @param 无
 * @retval uint32_t: UART 异常次数
 */
uint32_t Modbus_Slave_GetUartErrorCount(void);

/**
 * @brief 获取 UART overrun 错误累计次数。
 * @param 无
 * @retval uint32_t: overrun 错误次数
 */
uint32_t Modbus_Slave_GetUartOverrunCount(void);

/**
 * @brief 获取 UART 帧错误累计次数。
 * @param 无
 * @retval uint32_t: 帧错误次数
 */
uint32_t Modbus_Slave_GetUartFrameErrorCount(void);

/**
 * @brief 获取 UART 噪声错误累计次数。
 * @param 无
 * @retval uint32_t: 噪声错误次数
 */
uint32_t Modbus_Slave_GetUartNoiseErrorCount(void);

/**
 * @brief 获取接收缓冲溢出累计次数。
 * @param 无
 * @retval uint32_t: 接收缓冲溢出次数
 */
uint32_t Modbus_Slave_GetRxOverflowCount(void);

#endif
