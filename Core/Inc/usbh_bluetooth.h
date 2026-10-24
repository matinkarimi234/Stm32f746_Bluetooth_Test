#ifndef USBH_BLUETOOTH_H
#define USBH_BLUETOOTH_H

#include "usbh_core.h"

#define USB_BT_CLASS       0xE0U
#define USB_BT_SUBCLASS    0x01U
#define USB_BT_PROTOCOL    0x01U

extern USBH_ClassTypeDef USBH_Bluetooth_Class;

extern volatile uint8_t BT_InterfaceFound;

#endif
