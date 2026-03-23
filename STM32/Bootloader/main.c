#include "main.h"
#include "bootloader.h"

static void Bootloader_SystemInit(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);
    usart_init(115200);
    LED_Init();
}

int main(void)
{
    Bootloader_SystemInit();
    Bootloader_Run();

    while (1)
    {
    }
}
