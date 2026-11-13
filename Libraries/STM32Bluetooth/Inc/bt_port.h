#ifndef BT_PORT_H
#define BT_PORT_H
#include <stdint.h>
/* Called cooperatively, never from an interrupt. Return 1 on success, 0 on error.
 * Read must handle any byte span within BT_BONDS_STORAGE_BYTES.
 * WriteRecord must program the body before the first (magic) word. Never erase.
 * Erase is called only by the explicit BT_Bonds_ClearAll maintenance operation.
 * Storage must survive reset. Keep it outside the firmware load/erase regions. */
uint8_t BT_Port_BondRead(uint32_t offset, void *data, uint32_t length);
uint8_t BT_Port_BondWriteRecord(uint32_t offset, const void *data, uint32_t length);
uint8_t BT_Port_BondErase(void);
uint32_t BT_Port_GetTick(void);
#endif
