#include "DWT.h"

/**
 * @brief 初始化DWT计数器
 * 
 * 使能Cortex-M3内核的DWT模块并初始化CYCCNT周期计数器，
 * 该计数器将以CPU内核时钟频率运行，为延时函数提供时间基准。
 * 
 * 初始化步骤：
 * 1. 使能DEMCR寄存器中的TRCENA位，解锁DWT模块
 * 2. 清零CYCCNT计数器，确保计数从0开始
 * 3. 使能CYCCNT计数器，开始计数
 * 
 * @note 必须在使用任何延时函数之前调用此函数
 * @note 调试器连接可能会影响DWT计数器的正常工作
 */
void DWT_Init(void)
{
	//上电延时, 否则DWT程序卡死
    for (uint16_t i = 0; i < 1000; i ++)
	{
		for (uint16_t j = 0; j < 1000; j ++);
	}
    // 使能DWT外设（DEMCR: Debug Exception and Monitor Control Register）
    // TRCENA位（bit24）：使能跟踪单元，包括DWT和ITM
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    
    // 清零CYCCNT计数器（CYCCNT: Cycle Count Register）
    DWT->CYCCNT = 0;
    
    // 启用CYCCNT计数器（CTRL: Control Register）
    // CYCCNTENA位（bit0）：使能CYCCNT计数器
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}


/**
  * @brief  读取当前时间戳
  * @param  无
  * @retval 当前时间戳，即DWT_CYCCNT寄存器的值
  */
uint32_t DWT_GetTick(void)
{ 
    return ((uint32_t)DWT->CYCCNT);
}


/**
 * @brief 获取当前系统运行时间（以微秒为单位）
 * 
 * 基于DWT的CYCCNT计数器实现，返回从DWT_Init()调用后开始的累计时间。
 * 支持32位CYCCNT溢出后自动计算（通过无符号数溢出特性实现）。
 * 
 * 计算逻辑：
 * 微秒数 = 累计周期数 ÷ (系统主频(MHz))
 * 例如：72MHz主频下，1,000,000个周期 = 1,000,000 ÷ 72 ≈ 13888μs
 * 
 * @return uint64_t 当前时间（微秒），范围0 ~ 约59.6小时（72MHz下）
 */
uint32_t DWT_GetUs(uint32_t cycles)
{
    // 转换为微秒：周期数 ÷ (主频(MHz))，通过64位计算避免溢出
    return (uint32_t)(1000000.0 / SystemCoreClock * cycles);
}


/**
 * @brief 微秒级延时函数
 * 
 * 利用DWT的CYCCNT计数器实现高精度微秒级延时，
 * 在72MHz主频下每个微秒对应72个时钟周期，精度可达1个时钟周期。
 * 
 * 计算逻辑：
 * 所需周期数 = 系统时钟频率(MHz) × 延时微秒数
 * 例如：72MHz系统时钟下，10us需要的周期数 = 72 × 10 = 720个周期
 * 
 * @param us 延时时间，单位：微秒(us)
 * @note 最大延时受32位CYCCNT限制，72MHz下约为59.6秒
 */
void delay_us(uint32_t us)
{
    uint32_t start = DWT_GetTick();               // 记录起始计数值
    uint32_t cycles = SystemCoreClock / 1000000 * us;  // 计算所需周期数
    
    // 等待计数器达到目标值（处理溢出情况，利用无符号数减法特性）
    while((DWT_GetTick() - start) < cycles);
}

/**
 * @brief 毫秒级延时函数
 * 
 * 基于微秒级延时函数实现，通过循环调用微秒延时来累积得到毫秒级延时。
 * 采用循环方式可以避免大数值计算导致的溢出问题。
 * 
 * 实现逻辑：
 * 1ms = 1000us，因此每次循环调用delay_us(1000)并递减计数
 * 
 * @param ms 延时时间，单位：毫秒(ms)
 * @note 适合中等长度的延时需求（如传感器初始化、状态等待等）
 */
void delay_ms(uint16_t ms)
{
    // 循环调用微秒延时，每次处理1ms
    while(ms--)
    {
        delay_us(1000);
    }
}

/**
 * @brief 秒级延时函数
 * 
 * 基于毫秒级延时函数实现，通过循环调用毫秒延时来累积得到秒级延时。
 * 采用循环方式可以处理较长时间的延时需求。
 * 
 * 实现逻辑：
 * 1s = 1000ms，因此每次循环调用delay_ms(1000)并递减计数
 * 
 * @param s 延时时间，单位：秒(s)
 * @note 适合较长时间的延时需求（如系统休眠前等待、超时检测等）
 * @note 长时间延时会占用CPU资源，若需低功耗可考虑在循环中加入休眠指令
 */
void delay_s(uint32_t s)
{
    // 循环调用毫秒延时，每次处理1秒
    while(s--)
    {
        delay_ms(1000);
    }
}
