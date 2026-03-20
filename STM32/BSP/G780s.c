#include "G780s.h"
#include "Modbus_Slave.h"
#include <stdio.h>
#include <string.h>

static uint16_t g_registers[MODBUS_REG_COUNT];

/**
 * @brief 读取 G780S 业务寄存器镜像中的一段寄存器。
 * @param start_addr: 起始寄存器地址
 * @param reg_count: 连续读取的寄存器数量
 * @param out_regs: 输出缓冲区
 * @param context: 业务上下文，当前未使用
 * @retval 0 表示成功，非 0 为 Modbus 异常码
 */
/* G780S 业务层只维护寄存器镜像，真正的 RTU 收发由 Modbus_Slave 负责。 */
static uint8_t G780s_ReadRegisters(uint16_t start_addr,
                                   uint16_t reg_count,
                                   uint16_t *out_regs,
                                   void *context)
{
    (void)context;

    if (out_regs == NULL)
    {
        return 0x03;
    }

    /* 业务层统一做地址边界检查，返回标准 Modbus 异常码。 */
    if ((uint32_t)start_addr + reg_count > MODBUS_REG_COUNT)
    {
        return 0x02;
    }

    /* 读寄存器和主循环写寄存器可能并发发生，这里短暂关中断保证一致性。 */
    __disable_irq();
    for (uint16_t i = 0; i < reg_count; i++)
    {
        out_regs[i] = g_registers[start_addr + i];
    }
    __enable_irq();

    return 0;
}

/**
 * @brief 处理云端写单寄存器命令，只允许写继电器相关寄存器。
 * @param reg_addr: 目标寄存器地址
 * @param reg_value: 待写入的寄存器值
 * @param context: 业务上下文，当前未使用
 * @retval 0 表示成功，非 0 为 Modbus 异常码
 */
static uint8_t G780s_WriteSingleRegister(uint16_t reg_addr,
                                         uint16_t reg_value,
                                         void *context)
{
    (void)context;

    /* 当前只允许云端改继电器相关寄存器，其它地址一律拒绝。 */
    if (reg_addr != REG_RELAY_CTRL && reg_addr != REG_RELAY_CMD_BITS)
    {
        return 0x02;
    }

    g_registers[reg_addr] = reg_value;
    return 0;
}

/**
 * @brief 处理云端写单线圈命令，把线圈状态映射到继电器命令位图。
 * @param coil_addr: 线圈地址
 * @param coil_value: 线圈目标状态，true 为置位
 * @param context: 业务上下文，当前未使用
 * @retval 0 表示成功，非 0 为 Modbus 异常码
 */
static uint8_t G780s_WriteSingleCoil(uint16_t coil_addr,
                                     bool coil_value,
                                     void *context)
{
    uint16_t bit_num;
    uint16_t current;

    (void)context;

    /* 将线圈地址按位映射到继电器命令位图，供主循环再执行实际动作。 */
    bit_num = (uint16_t)(coil_addr % 16U);
    if (bit_num > 15U)
    {
        return 0x02;
    }

    current = g_registers[REG_RELAY_CMD_BITS];
    if (coil_value)
    {
        current |= (uint16_t)(1u << bit_num);
    }
    else
    {
        current &= (uint16_t)~(1u << bit_num);
    }

    g_registers[REG_RELAY_CMD_BITS] = current;
    return 0;
}

/**
 * @brief 初始化 G780S 业务层，并把寄存器回调注册到通用从站引擎。
 * @param 无
 * @retval 无
 */
void G780s_Init(void)
{
    ModbusSlaveConfig config = {
        .slave_addr = MODBUS_SLAVE_ADDR,
        .context = NULL,
        .read_holding_registers = G780s_ReadRegisters,
        .read_input_registers = G780s_ReadRegisters,
        .write_single_register = G780s_WriteSingleRegister,
        .write_single_coil = G780s_WriteSingleCoil,
    };

    /* 注册 G780S 的寄存器读写回调，把通用从站引擎和业务寄存器绑定起来。 */
    memset(g_registers, 0, sizeof(g_registers));
    Modbus_Slave_Init(&config);
    printf("[G780s] Init OK (Addr=%d, USART3+PA5, layered RTU)\r\n", MODBUS_SLAVE_ADDR);
}

/**
 * @brief 在主循环中处理 G780S 对应的 Modbus 从站任务。
 * @param 无
 * @retval 无
 */
void G780s_Process(void)
{
    /* 主循环周期调用，真正执行“帧静默判定 -> 解析 -> 回包”。 */
    Modbus_Slave_Process();
}

/**
 * @brief 兼容旧接口的字节接收入口，内部转发给通用从站引擎。
 * @param byte: 串口中断收到的单个字节
 * @retval 无
 */
void G780s_RxCallback(uint8_t byte)
{
    /* 兼容旧接口：字节接收已下沉到 Modbus_Slave，这里仅做转发。 */
    Modbus_Slave_RxCallback(byte);
}

/**
 * @brief 把现场采集值刷新到 G780S 寄存器镜像，供云端轮询读取。
 * @param data: 最新业务数据快照
 * @retval 无
 */
void G780s_UpdateData(const G780sSlaveData *data)
{
    if (data == NULL)
    {
        return;
    }

    /* 主循环把现场采集值刷新到寄存器镜像，供 G780S / 云平台随时读取。 */
    __disable_irq();

    g_registers[REG_PUSH_SEQ] = data->push_seq;

    g_registers[REG_PT100_CH1] = (uint16_t)data->pt100_ch[0];
    g_registers[REG_PT100_CH2] = (uint16_t)data->pt100_ch[1];
    g_registers[REG_PT100_CH3] = (uint16_t)data->pt100_ch[2];
    g_registers[REG_PT100_CH4] = (uint16_t)data->pt100_ch[3];

    g_registers[REG_WEIGHT_CH1_H] = (uint16_t)((data->weight_ch[0] >> 16) & 0xFFFF);
    g_registers[REG_WEIGHT_CH1_L] = (uint16_t)(data->weight_ch[0] & 0xFFFF);
    g_registers[REG_WEIGHT_CH2_H] = (uint16_t)((data->weight_ch[1] >> 16) & 0xFFFF);
    g_registers[REG_WEIGHT_CH2_L] = (uint16_t)(data->weight_ch[1] & 0xFFFF);
    g_registers[REG_WEIGHT_CH3_H] = (uint16_t)((data->weight_ch[2] >> 16) & 0xFFFF);
    g_registers[REG_WEIGHT_CH3_L] = (uint16_t)(data->weight_ch[2] & 0xFFFF);
    g_registers[REG_WEIGHT_CH4_H] = (uint16_t)((data->weight_ch[3] >> 16) & 0xFFFF);
    g_registers[REG_WEIGHT_CH4_L] = (uint16_t)(data->weight_ch[3] & 0xFFFF);

    g_registers[REG_FLOW_RATE] = data->flow_rate;
    g_registers[REG_FLOW_TOTAL_HIGH] = (uint16_t)((data->flow_total >> 16) & 0xFFFF);
    g_registers[REG_FLOW_TOTAL_LOW] = (uint16_t)(data->flow_total & 0xFFFF);

    g_registers[REG_RELAY_DO] = data->relay_do;
    g_registers[REG_RELAY_DI] = data->relay_di;
    g_registers[REG_RELAY_BITS] = data->relay_do;
    g_registers[REG_SYSTEM_STATUS] = data->status;

    __enable_irq();
}

/**
 * @brief 获取 G780S 从站底层 UART 句柄。
 * @param 无
 * @retval UART_HandleTypeDef*: USART3 句柄指针
 */
UART_HandleTypeDef *G780s_GetHandle(void)
{
    return Modbus_Slave_GetHandle();
}

/**
 * @brief 获取继电器翻转控制寄存器的当前值。
 * @param 无
 * @retval 当前寄存器值
 */
uint16_t G780s_GetRelayCtrl(void)
{
    return g_registers[REG_RELAY_CTRL];
}

/**
 * @brief 获取继电器按位命令位图寄存器的当前值。
 * @param 无
 * @retval 当前寄存器值
 */
uint16_t G780s_GetRelayBits(void)
{
    return g_registers[REG_RELAY_CMD_BITS];
}
