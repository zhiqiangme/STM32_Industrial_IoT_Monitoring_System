#include "bootloader.h"

#include "LED.h"
#include "Upgrade.h"
#include "delay.h"
#include "usart.h"

#include <stdio.h>

static void Bootloader_PrintBanner(void)
{
    printf("\r\n[BOOT] STM32 Mill Bootloader\r\n");
    printf("[BOOT] APP base: 0x%08lX\r\n", (unsigned long)UPGRADE_APP_BASE_ADDR);
    printf("[BOOT] State page: 0x%08lX\r\n", (unsigned long)UPGRADE_STATE_PAGE_ADDR);
}

uint8_t Bootloader_ShouldStayInLoader(void)
{
    UpgradeStateImage state;
    uint8_t state_ok = Upgrade_LoadState(&state);

    if (state_ok == 0u && state.state != UPGRADE_STATE_IDLE && state.state != UPGRADE_STATE_DONE)
    {
        return 1u;
    }

    return (Upgrade_IsAppVectorValid(UPGRADE_APP_BASE_ADDR) == 0u) ? 1u : 0u;
}

void Bootloader_Run(void)
{
    UpgradeStateImage state;
    uint32_t last_tick = 0u;

    Bootloader_PrintBanner();

    if (Upgrade_LoadState(&state) == 0u)
    {
        printf("[BOOT] state=%u source=%u err=%u written=%lu\r\n",
               state.state,
               state.request_source,
               state.error_code,
               (unsigned long)state.written_bytes);
    }
    else
    {
        printf("[BOOT] state page empty or invalid, fallback policy active\r\n");
    }

    if (Bootloader_ShouldStayInLoader() == 0u)
    {
        printf("[BOOT] valid app found, jump now\r\n");
        HAL_Delay(10);
        Upgrade_JumpToApplication(UPGRADE_APP_BASE_ADDR);
    }

    printf("[BOOT] staying in loader, protocol worker not implemented yet\r\n");

    while (1)
    {
        if ((HAL_GetTick() - last_tick) >= 500u)
        {
            last_tick = HAL_GetTick();
            LED_R_TOGGLE();
        }
    }
}
