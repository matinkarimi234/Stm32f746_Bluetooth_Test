#ifndef BT_USER_CONFIG_H
#define BT_USER_CONFIG_H
/* This example deliberately retains the tested development pairing policy. */
#define BT_DEVICE_NAME "STM32 Bluetooth SPP"
#define BT_PAIRING_TEST_MODE 0U
#define BT_AUTO_ACCEPT_CONNECTIONS 1U
#define BT_AUTO_AUTH_TEST 0U
#define BT_SPP_RFCOMM_CHANNEL 1U
#define BT_BONDS_MAX_DEVICES 16U
#define BT_BONDS_STORAGE_BYTES 0x00040000UL
/* Board port only. Must match the linker reservation; never auto-erased. */
#define BT_BONDS_FLASH_START 0x080C0000UL
#define BT_BONDS_FLASH_BYTES BT_BONDS_STORAGE_BYTES
#endif
