#ifndef BT_BONDS_H
#define BT_BONDS_H
#include <stdint.h>

/* STM32F746NG, 1 MB flash. Sector 7 is reserved in the .sct file. */
#define BT_BONDS_FLASH_START  0x080C0000UL
#define BT_BONDS_FLASH_BYTES  0x00040000UL
#define BT_BONDS_MAX_DEVICES  16U

extern volatile uint8_t BT_Bonds_Count;
extern volatile uint8_t BT_Bonds_Dirty;
extern volatile uint32_t BT_Bonds_Writes;
extern volatile uint32_t BT_Bonds_FlashErrors;
extern volatile uint32_t BT_Bonds_Reused;
extern volatile uint32_t BT_Bonds_Evictions;
extern volatile uint8_t BT_Bonds_Full;

void BT_Bonds_Init(void);
uint8_t BT_Bonds_Find(const uint8_t bd_addr_hci[6], uint8_t key_out[16]);
void BT_Bonds_Remember(const uint8_t bd_addr_hci[6], const uint8_t key[16], uint8_t key_type);
/* Call from cooperative main loop. Writes pending entries only when ACL disconnected. */
void BT_Bonds_Process(void);
/* Explicit factory-reset operation: erases ALL remembered bonds. */
uint8_t BT_Bonds_ClearAll(void);

#endif
