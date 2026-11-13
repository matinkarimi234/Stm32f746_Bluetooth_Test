# STM32Bluetooth

Bluetooth Classic SPP library used by this firmware. Hardware reported working
with the current STM32F746G-DISCO / CSR-compatible 0A12:0001 setup.

The Keil project builds Src/ and Ports/STM32F746/ directly. Inc/bt_library.h is the
application API. Firmware/Core/Inc/bt_user_config.h holds this example's settings;
BT_USE_USER_CONFIG is enabled in Keil. Application echo is in app_bluetooth.c.
Register after USBH_Init and before USBH_Start. Poll BT_Library_Process after
USBH_Process. Use the existing MDK-ARM/watch.ini to inspect connection state.

The sector-7 bond store is reserved by the linker. Keep that region during flash
updates. This example explicitly enables development pairing; generic template
defaults disable it. The library supports one host and one active SPP stream.

Reusable driver development, tests, MIT license and CubeMX packaging:
https://github.com/matinkarimi234/STM32-USB-Bluetooth-SPP
