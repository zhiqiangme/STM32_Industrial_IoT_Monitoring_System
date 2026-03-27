#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "Upgrade.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef struct
{
    UART_HandleTypeDef uart;
    UpgradeStateImage state;
    UpgradeBootControl boot_control;
    uint16_t boot_slot;
    uint16_t transfer_slot;
    uint16_t last_error;
    uint8_t reset_pending;
} BootloaderRuntime;

/* Bootloader 主流程：决定是否停留、处理升级协议、必要时跳转到 App。 */
void Bootloader_Run(void);
/* 上电后根据状态页与 App 有效性判断是否继续停在 Bootloader。 */
uint8_t Bootloader_ShouldStayInLoader(void);
/* USART3 中断入口转发到 Bootloader 串口句柄。 */
void Bootloader_Usart3IrqHandler(void);

#endif
