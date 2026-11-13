#include "bt_library.h"
#include "usbh_bluetooth.h"
#include "bt_spp.h"
#include "bt_bonds.h"
#include <string.h>
static USBH_HandleTypeDef *registered_host;
static BT_LibraryCallbacks application;
static uint8_t last_serial_open;
static void receive(const uint8_t *data, uint16_t length, void *context)
{
    (void)context;
    if (application.received != NULL) { application.received(data, length, application.context); }
}
USBH_StatusTypeDef BT_Library_Register(USBH_HandleTypeDef *host, const BT_LibraryCallbacks *callbacks)
{
    USBH_StatusTypeDef result;
    if (host == NULL) { return USBH_FAIL; }
    if (registered_host != NULL) { return USBH_BUSY; }
    result = USBH_RegisterClass(host, &USBH_Bluetooth_Class);
    if (result != USBH_OK) { return result; }
    memset(&application, 0, sizeof(application));
    if (callbacks != NULL) { application = *callbacks; }
    registered_host = host;
    last_serial_open = 0U;
    BT_SPP_SetReceiver(receive, NULL);
    return USBH_OK;
}
uint8_t BT_Library_IsSerialOpen(void)
{ return (uint8_t)(registered_host != NULL && BT_Connected && BT_SPP_SerialOpen && BT_Endpoints.PipesReady); }
uint8_t BT_Library_Send(const uint8_t *data, uint16_t length)
{
    if (!BT_Library_IsSerialOpen()) { return 0U; }
    return BT_SPP_Send(data, length);
}
void BT_Library_Process(void)
{
    uint8_t open;
    if (registered_host == NULL) { return; }
    BT_Bonds_Process();
    open = BT_Library_IsSerialOpen();
    if (open != last_serial_open)
    {
        last_serial_open = open;
        if (application.serial_changed != NULL) { application.serial_changed(open, application.context); }
    }
}
