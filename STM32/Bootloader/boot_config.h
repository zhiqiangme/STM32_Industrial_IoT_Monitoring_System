#ifndef __BOOT_CONFIG_H
#define __BOOT_CONFIG_H

#include "stm32f1xx_hal.h"

#define BOOT_INFO_VERSION                 0x0002u

#define BOOT_ERR_NONE                     0x0000u
#define BOOT_ERR_BAD_FRAME                0x0001u
#define BOOT_ERR_BAD_CMD                  0x0002u
#define BOOT_ERR_BAD_LENGTH               0x0003u
#define BOOT_ERR_BAD_STATE                0x0004u
#define BOOT_ERR_BAD_SIZE                 0x0005u
#define BOOT_ERR_FLASH_ERASE              0x0006u
#define BOOT_ERR_FLASH_PROGRAM            0x0007u
#define BOOT_ERR_BAD_OFFSET               0x0008u
#define BOOT_ERR_VERIFY                   0x0009u
#define BOOT_ERR_NOT_COMPLETE             0x000Au
#define BOOT_ERR_PACKET_CRC              0x000Bu
#define BOOT_ERR_BAD_CRC                 BOOT_ERR_PACKET_CRC
#define BOOT_ERR_TIMEOUT_RECOVERY         0x000Cu
#define BOOT_ERR_STATE_CRC               0x000Du
#define BOOT_ERR_VERIFY_CRC32            0x000Eu
#define BOOT_ERR_VERIFY_SHA256           0x000Fu
#define BOOT_ERR_VECTOR_INVALID          0x0010u
#define BOOT_ERR_WDG_RECOVERY            0x0011u

#define BOOT_UART                         USART3
#define BOOT_485_EN_PORT                  GPIOA
#define BOOT_485_EN_PIN                   GPIO_PIN_5
#define BOOT_UART_TX_PORT                 GPIOB
#define BOOT_UART_TX_PIN                  GPIO_PIN_10
#define BOOT_UART_RX_PORT                 GPIOB
#define BOOT_UART_RX_PIN                  GPIO_PIN_11

/* RS485 驱动方向切换稳定时间，单位 us。
 * 用 SysTick 精确延时替代旧的 for(volatile) 忙等，避免随编译器优化等级漂移。 */
#define BOOT_RS485_TX_SETTLE_US           50u
#define BOOT_RS485_RX_SETTLE_US           20u
#define BOOT_LED_TOGGLE_INTERVAL_MS       200u
#define BOOT_UART_READ_POLL_MS            20u

#define BOOT_FORCE_STAY_KEY_PORT          GPIOE
#define BOOT_FORCE_STAY_KEY_PIN           GPIO_PIN_4
#define BOOT_FORCE_STAY_KEY_ACTIVE_LEVEL  GPIO_PIN_RESET
#define BOOT_FORCE_STAY_KEY_CLK_ENABLE()  do { __HAL_RCC_GPIOE_CLK_ENABLE(); } while (0)
#define BOOT_FORCE_STAY_UART_WINDOW_MS    2500u
#define BOOT_FORCE_STAY_UART_MAGIC        ((uint8_t)'B')

#endif
