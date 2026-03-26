#include "bootloader.h"

#include "boot_main.h"

void Bootloader_Run(void)
{
    BootMain_Run();
}

uint8_t Bootloader_ShouldStayInLoader(void)
{
    return BootMain_ShouldStayInLoader();
}

void Bootloader_Usart3IrqHandler(void)
{
    BootMain_Usart3IrqHandler();
}
