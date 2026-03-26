#ifndef __BOOT_PROTOCOL_H
#define __BOOT_PROTOCOL_H

#include "bootloader.h"
#include "ymodem.h"

#include <stdint.h>

void BootProtocol_PrintBanner(void);
void BootProtocol_InitUart(BootloaderRuntime *runtime);
void BootProtocol_PrintState(const BootloaderRuntime *runtime);
void BootProtocol_PrintWaitingMessage(void);
COM_StatusTypeDef BootProtocol_RunYmodemSession(BootloaderRuntime *runtime, YmodemReceiveResult *result);
UART_HandleTypeDef *BootProtocol_GetUartHandle(BootloaderRuntime *runtime);

#endif
