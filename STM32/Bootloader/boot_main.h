#ifndef __BOOT_MAIN_H
#define __BOOT_MAIN_H

#include <stdint.h>

void BootMain_Run(void);
uint8_t BootMain_ShouldStayInLoader(void);
void BootMain_Usart3IrqHandler(void);

#endif
