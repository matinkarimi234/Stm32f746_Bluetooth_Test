#include "bt_config.h"
#include "bt_port.h"
#include "stm32f7xx_hal.h"
#include <string.h>
#ifndef BT_BONDS_FLASH_START
#error Set BT_BONDS_FLASH_START in the project configuration and reserve sector 7
#endif
/* This port intentionally supports ONE verified erase geometry only. */
#if BT_BONDS_FLASH_START != 0x080C0000UL || BT_BONDS_STORAGE_BYTES != 0x00040000UL
#error This port requires STM32F746 1MB flash sector 7; supply another port for other layouts
#endif
uint32_t BT_Port_GetTick(void) { return HAL_GetTick(); }
uint8_t BT_Port_BondRead(uint32_t offset, void *data, uint32_t length)
{
    if (data == NULL || offset > BT_BONDS_STORAGE_BYTES || length > BT_BONDS_STORAGE_BYTES - offset) { return 0U; }
    memcpy(data, (const void *)(BT_BONDS_FLASH_START + offset), length);
    return 1U;
}
uint8_t BT_Port_BondWriteRecord(uint32_t offset, const void *data, uint32_t length)
{
    uint32_t i, word, address;
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t ok = 1U;
    if (data == NULL || length != 36U || offset % 36U != 0U ||
        offset > BT_BONDS_STORAGE_BYTES - length) { return 0U; }
    address = BT_BONDS_FLASH_START + offset;
    /* Refuse all writes to a non-erased slot, including a torn record. */
    for (i = 0U; i < length; i += 4U)
    { if (*(const volatile uint32_t *)(address + i) != 0xFFFFFFFFUL) { return 0U; } }
    if (HAL_FLASH_Unlock() != HAL_OK) { return 0U; }
    for (i = 4U; i < length; i += 4U)
    {
        memcpy(&word, bytes + i, 4U);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i, word) != HAL_OK) { ok = 0U; break; }
    }
    memcpy(&word, bytes, 4U);
    if (ok && HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, word) != HAL_OK) { ok = 0U; }
    (void)HAL_FLASH_Lock();
    return ok;
}
uint8_t BT_Port_BondErase(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0UL;
    HAL_StatusTypeDef result;
    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = FLASH_SECTOR_7;
    erase.NbSectors = 1U;
    if (HAL_FLASH_Unlock() != HAL_OK) { return 0U; }
    result = HAL_FLASHEx_Erase(&erase, &sector_error);
    (void)HAL_FLASH_Lock();
    return (result == HAL_OK) ? 1U : 0U;
}
