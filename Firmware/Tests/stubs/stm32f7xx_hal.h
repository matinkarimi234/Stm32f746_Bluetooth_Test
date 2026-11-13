#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef struct { uint32_t TypeErase, VoltageRange, Sector, NbSectors; } FLASH_EraseInitTypeDef;
#define FLASH_TYPEPROGRAM_WORD 0
#define FLASH_TYPEERASE_SECTORS 0
#define FLASH_VOLTAGE_RANGE_3 0
#define FLASH_SECTOR_7 7
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t, uint32_t, uint64_t);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *, uint32_t *);
