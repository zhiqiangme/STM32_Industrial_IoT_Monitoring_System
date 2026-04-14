#include "main.h"
#include "bootloader.h"

/* Bootloader 只保留最小运行时：系统时钟、串口1日志、LED 和向量表基址。 */
static void Bootloader_SystemInit(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);
    usart_init(115200);
    LED_Init();
    SCB->VTOR = FLASH_BASE;
}

int main(void)
{
    /* 启动后始终进入 Bootloader 主流程，由它决定是驻留还是跳 App。 */
    Bootloader_SystemInit();
    Bootloader_Run();

    while (1)
    {
    }
}
