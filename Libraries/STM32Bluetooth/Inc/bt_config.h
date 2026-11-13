#ifndef BT_CONFIG_H
#define BT_CONFIG_H
/* Define BT_USE_USER_CONFIG consistently for every library translation unit. */
#ifdef BT_USE_USER_CONFIG
#include "bt_user_config.h"
#endif
#ifndef BT_DEVICE_NAME
#define BT_DEVICE_NAME "STM32 Bluetooth SPP"
#endif
#ifndef BT_PAIRING_TEST_MODE
#define BT_PAIRING_TEST_MODE 0U
#endif
#ifndef BT_AUTO_ACCEPT_CONNECTIONS
#define BT_AUTO_ACCEPT_CONNECTIONS 1U
#endif
#ifndef BT_AUTO_AUTH_TEST
#define BT_AUTO_AUTH_TEST 0U
#endif
#ifndef BT_SPP_RFCOMM_CHANNEL
#define BT_SPP_RFCOMM_CHANNEL 1U
#endif
#ifndef BT_BONDS_MAX_DEVICES
#define BT_BONDS_MAX_DEVICES 16U
#endif
#ifndef BT_BONDS_STORAGE_BYTES
#define BT_BONDS_STORAGE_BYTES 0x00040000UL
#endif
#if BT_BONDS_MAX_DEVICES < 1 || BT_BONDS_MAX_DEVICES > 16
#error Bond device count must be between 1 and 16
#endif
#if BT_SPP_RFCOMM_CHANNEL < 1 || BT_SPP_RFCOMM_CHANNEL > 30
#error RFCOMM server channel must be between 1 and 30
#endif
#if BT_BONDS_STORAGE_BYTES < 36 || BT_BONDS_STORAGE_BYTES / 36 > 65535
#error Bond journal capacity does not fit the slot index
#endif
#endif
