#ifndef USBH_BLUETOOTH_H
#define USBH_BLUETOOTH_H

#include "usbh_core.h"

#define USB_BT_CLASS       0xE0U
#define USB_BT_SUBCLASS    0x01U
#define USB_BT_PROTOCOL    0x01U

extern USBH_ClassTypeDef USBH_Bluetooth_Class;

extern volatile uint8_t BT_InterfaceFound;


typedef struct
{
    uint8_t  EventInAddr;
    uint16_t EventInMPS;
    uint8_t  EventInterval;

    uint8_t  ACLInAddr;
    uint16_t ACLInMPS;

    uint8_t  ACLOutAddr;
    uint16_t ACLOutMPS;

    uint8_t  EndpointsReady;
	
		uint8_t EventInPipe;
		uint8_t ACLInPipe;
		uint8_t ACLOutPipe;

		uint8_t PipesReady;

} BT_EndpointInfo_t;

extern volatile BT_EndpointInfo_t BT_Endpoints;


#define BT_EVENT_BUFFER_SIZE 64U

// Event receive states
#define BT_EVENT_RX_SUBMIT   0U
#define BT_EVENT_RX_WAIT     1U
#define BT_EVENT_RX_DELAY    2U
#define BT_EVENT_RX_ERROR    3U

// Debug variables
extern volatile uint8_t BT_EventRxState;

extern volatile uint32_t BT_EventSubmitCount;
extern volatile uint32_t BT_EventPacketCount;
extern volatile uint32_t BT_EventNAKCount;
extern volatile uint32_t BT_EventErrorCount;

extern volatile uint16_t BT_LastEventLength;
extern volatile uint8_t BT_LastEventData[BT_EVENT_BUFFER_SIZE];

/* HCI Reset state machine */
#define BT_HCI_RESET_IDLE          0U
#define BT_HCI_RESET_SEND          1U
#define BT_HCI_RESET_USB_WAIT      2U
#define BT_HCI_RESET_EVENT_WAIT    3U
#define BT_HCI_RESET_OK            4U
#define BT_HCI_RESET_ERROR         5U

extern volatile uint8_t BT_HCI_ResetState;
extern volatile uint8_t BT_HCI_ResetUSBComplete;
extern volatile uint8_t BT_HCI_ResetEventSeen;
extern volatile uint8_t BT_HCI_ResetStatus;


#define BT_VERSION_WAIT_RESET  0U
#define BT_VERSION_SEND        1U
#define BT_VERSION_USB_WAIT    2U
#define BT_VERSION_EVENT_WAIT  3U
#define BT_VERSION_OK          4U
#define BT_VERSION_ERROR       5U

typedef struct
{
    uint8_t  HCI_Version;
    uint16_t HCI_Revision;
    uint8_t  LMP_Version;
    uint16_t Manufacturer;
    uint16_t LMP_Subversion;

} BT_VersionInfo_t;

extern volatile BT_VersionInfo_t BT_VersionInfo;
extern volatile uint8_t BT_VersionState;
extern volatile uint8_t BT_VersionUSBComplete;
extern volatile uint8_t BT_VersionEventSeen;
extern volatile uint8_t BT_VersionStatus;


/* Bluetooth Device Address states */
#define BT_ADDR_WAIT_VERSION  0U
#define BT_ADDR_SEND          1U
#define BT_ADDR_USB_WAIT      2U
#define BT_ADDR_EVENT_WAIT    3U
#define BT_ADDR_OK            4U
#define BT_ADDR_ERROR         5U

extern volatile uint8_t BT_AddrState;
extern volatile uint8_t BT_AddrUSBComplete;
extern volatile uint8_t BT_AddrEventSeen;
extern volatile uint8_t BT_AddrStatus;

/* Bluetooth address, most significant byte first */
extern volatile uint8_t BT_BD_ADDR[6];

#endif
