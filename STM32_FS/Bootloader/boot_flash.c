#include "boot_flash.h"

#include "boot_config.h"

#include <string.h>

static uint16_t BootFlash_ResolveStableSlot(const BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return UPGRADE_SLOT_A;
    }

    if (Upgrade_IsSlotIdValid(runtime->boot_control.confirmed_slot) != 0u)
    {
        return runtime->boot_control.confirmed_slot;
    }
    if (Upgrade_IsSlotIdValid(runtime->boot_control.active_slot) != 0u)
    {
        return runtime->boot_control.active_slot;
    }

    return UPGRADE_SLOT_A;
}

static uint16_t BootFlash_SelectDownloadSlot(const BootloaderRuntime *runtime)
{
    uint16_t stable_slot = BootFlash_ResolveStableSlot(runtime);
    uint16_t other_slot = Upgrade_GetOtherSlot(stable_slot);

    return (Upgrade_IsSlotIdValid(other_slot) != 0u) ? other_slot : UPGRADE_SLOT_B;
}

/* 这些状态说明上次升级尚未结束，Bootloader 默认应继续驻留等待恢复。 */
uint8_t BootFlash_IsUpgradeActiveState(uint16_t state)
{
    return (state == UPGRADE_STATE_REQUESTED ||
            state == UPGRADE_STATE_ERASING ||
            state == UPGRADE_STATE_PROGRAMMING ||
            state == UPGRADE_STATE_VERIFYING) ? 1u : 0u;
}

uint8_t BootFlash_LoadState(BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return 1u;
    }

    return Upgrade_LoadState(&runtime->state);
}

void BootFlash_InitState(BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return;
    }

    Upgrade_InitStateImage(&runtime->state);
}

uint8_t BootFlash_LoadBootControl(BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return 1u;
    }

    return Upgrade_LoadBootControl(&runtime->boot_control);
}

void BootFlash_InitBootControl(BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return;
    }

    Upgrade_InitBootControl(&runtime->boot_control);
}

uint8_t BootFlash_SaveBootControl(BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return 1u;
    }

    return Upgrade_SaveBootControl(&runtime->boot_control);
}

/* START 阶段尽量沿用 App 先前写入状态页的请求来源，避免把远程请求覆盖成 LOCAL。 */
uint16_t BootFlash_ResolveRequestSource(void)
{
    UpgradeStateImage previous_state;

    if (Upgrade_LoadState(&previous_state) == 0u &&
        BootFlash_IsUpgradeActiveState(previous_state.state) != 0u &&
        previous_state.request_source != UPGRADE_REQUEST_SOURCE_NONE)
    {
        return previous_state.request_source;
    }

    return UPGRADE_REQUEST_SOURCE_LOCAL;
}

/* 如果升级头里没显式传目标版本，则保留 App 阶段已经写入状态页的值。 */
uint32_t BootFlash_ResolveTargetVersion(uint32_t requested_target_fw_version)
{
    UpgradeStateImage previous_state;

    if (requested_target_fw_version != 0u)
    {
        return requested_target_fw_version;
    }

    if (Upgrade_LoadState(&previous_state) == 0u &&
        BootFlash_IsUpgradeActiveState(previous_state.state) != 0u)
    {
        return previous_state.target_fw_version;
    }

    return 0u;
}

void BootFlash_MarkFailed(BootloaderRuntime *runtime, uint16_t error_code)
{
    if (runtime == NULL)
    {
        return;
    }

    if (runtime->state.magic != UPGRADE_STATE_MAGIC)
    {
        Upgrade_InitStateImage(&runtime->state);
    }

    runtime->state.state = UPGRADE_STATE_FAILED;
    runtime->state.error_code = error_code;
    runtime->state.active_boot_count = 0u;
    (void)Upgrade_SaveState(&runtime->state);
    runtime->last_error = error_code;
}

/* 避免每包都刷状态页，默认每写满 2KB 页边界或最后一包时再持久化一次。 */
uint8_t BootFlash_SaveStateThrottled(BootloaderRuntime *runtime, uint8_t force_save)
{
    if (runtime == NULL)
    {
        return 1u;
    }

    if (force_save != 0u || (runtime->state.written_bytes % 2048u) == 0u)
    {
        return Upgrade_SaveState(&runtime->state);
    }

    return 0u;
}

COM_StatusTypeDef BootFlash_BeginImage(BootloaderRuntime *runtime,
                                       uint32_t file_size,
                                       uint32_t image_crc32,
                                       uint32_t target_fw_version,
                                       const uint8_t *image_sha256)
{
    uint8_t err;

    if (runtime == NULL)
    {
        return COM_ERROR;
    }

    if (file_size == 0u || file_size > UPGRADE_APP_MAX_SIZE)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_BAD_SIZE);
        return COM_LIMIT;
    }

    runtime->transfer_slot = BootFlash_SelectDownloadSlot(runtime);
    if (Upgrade_IsSlotIdValid(runtime->transfer_slot) == 0u)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_BAD_STATE);
        return COM_ERROR;
    }

    Upgrade_InitStateImage(&runtime->state);
    runtime->state.state = UPGRADE_STATE_ERASING;
    runtime->state.request_source = BootFlash_ResolveRequestSource();
    runtime->state.target_fw_version = BootFlash_ResolveTargetVersion(target_fw_version);
    runtime->state.image_size = file_size;
    runtime->state.image_crc32 = image_crc32;
    if (image_sha256 != NULL)
    {
        memcpy(runtime->state.image_sha256, image_sha256, sizeof(runtime->state.image_sha256));
    }
    else
    {
        memset(runtime->state.image_sha256, 0, sizeof(runtime->state.image_sha256));
    }
    runtime->state.written_bytes = 0u;
    runtime->state.last_ok_offset = 0u;
    runtime->state.error_code = BOOT_ERR_NONE;
    runtime->state.active_boot_count = 0u;
    runtime->boot_control.pending_slot = UPGRADE_SLOT_NONE;
    runtime->boot_control.boot_attempts = 0u;
    Upgrade_ClearSlotRecord(&runtime->boot_control.slots[runtime->transfer_slot]);

    if (Upgrade_SaveState(&runtime->state) != 0u)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_FLASH_ERASE);
        return COM_DATA;
    }
    if (BootFlash_SaveBootControl(runtime) != 0u)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_FLASH_ERASE);
        return COM_DATA;
    }

    err = Upgrade_EraseSlotRegion(runtime->transfer_slot, file_size);
    if (err != 0u)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_FLASH_ERASE);
        return COM_DATA;
    }

    runtime->state.state = UPGRADE_STATE_PROGRAMMING;
    runtime->state.error_code = BOOT_ERR_NONE;
    runtime->state.active_boot_count = 0u;
    if (Upgrade_SaveState(&runtime->state) != 0u)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_FLASH_ERASE);
        return COM_DATA;
    }

    runtime->last_error = BOOT_ERR_NONE;
    return COM_OK;
}

COM_StatusTypeDef BootFlash_WriteImageData(BootloaderRuntime *runtime,
                                           uint32_t offset,
                                           const uint8_t *data,
                                           uint32_t len)
{
    uint8_t err;

    if (runtime == NULL)
    {
        return COM_ERROR;
    }

    if (len == 0u || data == NULL)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_BAD_LENGTH);
        return COM_DATA;
    }
    if (runtime->state.state != UPGRADE_STATE_PROGRAMMING)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_BAD_STATE);
        return COM_DATA;
    }
    if (offset != runtime->state.written_bytes)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_BAD_OFFSET);
        return COM_DATA;
    }
    if ((offset + len) > runtime->state.image_size)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_BAD_SIZE);
        return COM_DATA;
    }

    err = Upgrade_ProgramSlotBytes(runtime->transfer_slot, offset, data, len);
    if (err != 0u)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_FLASH_PROGRAM);
        return COM_DATA;
    }

    runtime->state.written_bytes = offset + len;
    runtime->state.last_ok_offset = offset + len;
    runtime->state.error_code = BOOT_ERR_NONE;
    runtime->state.active_boot_count = 0u;
    if (BootFlash_SaveStateThrottled(runtime,
                                     (runtime->state.written_bytes == runtime->state.image_size) ? 1u : 0u) != 0u)
    {
        BootFlash_MarkFailed(runtime, BOOT_ERR_FLASH_PROGRAM);
        return COM_DATA;
    }

    runtime->last_error = BOOT_ERR_NONE;
    return COM_OK;
}

uint8_t BootFlash_MarkVerifying(BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return 0u;
    }

    runtime->state.state = UPGRADE_STATE_VERIFYING;
    runtime->state.error_code = BOOT_ERR_NONE;
    runtime->state.active_boot_count = 0u;

    return (Upgrade_SaveState(&runtime->state) == 0u) ? 1u : 0u;
}

uint8_t BootFlash_MarkDone(BootloaderRuntime *runtime, uint32_t image_crc32)
{
    if (runtime == NULL)
    {
        return 0u;
    }

    runtime->state.state = UPGRADE_STATE_DONE;
    runtime->state.image_crc32 = image_crc32;
    runtime->state.error_code = BOOT_ERR_NONE;
    runtime->state.active_boot_count = 0u;
    runtime->last_error = BOOT_ERR_NONE;
    runtime->boot_control.pending_slot = runtime->transfer_slot;
    runtime->boot_control.boot_attempts = 0u;
    Upgrade_SetSlotRecord(&runtime->boot_control.slots[runtime->transfer_slot],
                          runtime->state.target_fw_version,
                          runtime->state.image_size,
                          image_crc32,
                          runtime->state.image_sha256);

    if (Upgrade_SaveState(&runtime->state) != 0u)
    {
        return 0u;
    }

    return (BootFlash_SaveBootControl(runtime) == 0u) ? 1u : 0u;
}
