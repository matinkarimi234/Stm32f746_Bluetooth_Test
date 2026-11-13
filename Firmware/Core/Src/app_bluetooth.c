#include "app_bluetooth.h"
#include "bt_spp.h"
/* Echo is sample application policy. A real product feeds its byte parser here. */
static void received(const uint8_t *data, uint16_t length, void *context)
{
    (void)context;
    if (!BT_Library_Send(data, length)) { BT_SPP_Debug.echo_drops++; }
}
USBH_StatusTypeDef APP_Bluetooth_Register(USBH_HandleTypeDef *host)
{
    BT_LibraryCallbacks callbacks;
    callbacks.received = received;
    callbacks.serial_changed = NULL;
    callbacks.context = NULL;
    return BT_Library_Register(host, &callbacks);
}
