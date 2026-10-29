
#ifndef USBH_BLUETOOTH_H
#define USBH_BLUETOOTH_H

#include "usbh_core.h"

#define USB_BT_CLASS                  0xE0U
#define USB_BT_SUBCLASS               0x01U
#define USB_BT_PROTOCOL               0x01U

#define BT_EVENT_BUFFER_SIZE          64U
#define BT_EVENT_RX_SUBMIT            0U
#define BT_EVENT_RX_WAIT              1U
#define BT_EVENT_RX_DELAY             2U
#define BT_EVENT_RX_ERROR             3U

#define BT_HCI_RESET_IDLE             0U
#define BT_HCI_RESET_SEND             1U
#define BT_HCI_RESET_USB_WAIT         2U
#define BT_HCI_RESET_EVENT_WAIT       3U
#define BT_HCI_RESET_OK               4U
#define BT_HCI_RESET_ERROR            5U

#define BT_VERSION_WAIT_RESET         0U
#define BT_VERSION_SEND               1U
#define BT_VERSION_USB_WAIT           2U
#define BT_VERSION_EVENT_WAIT         3U
#define BT_VERSION_OK                 4U
#define BT_VERSION_ERROR              5U

#define BT_ADDR_WAIT_VERSION          0U
#define BT_ADDR_SEND                  1U
#define BT_ADDR_USB_WAIT              2U
#define BT_ADDR_EVENT_WAIT            3U
#define BT_ADDR_OK                    4U
#define BT_ADDR_ERROR                 5U

#define BT_HCI_MAX_CMD_PARAMS         248U
#define BT_HCI_MAX_RESPONSE           64U
#define BT_HCI_CMD_IDLE               0U
#define BT_HCI_CMD_SEND               1U
#define BT_HCI_CMD_USB_WAIT           2U
#define BT_HCI_CMD_EVENT_WAIT         3U
#define BT_HCI_CMD_OK                 4U
#define BT_HCI_CMD_ERROR              5U

/* Initialization sequence */
#define BT_INIT_IDLE                  0U
#define BT_INIT_RESET_SEND            1U
#define BT_INIT_RESET_WAIT            2U
#define BT_INIT_VERSION_SEND          3U
#define BT_INIT_VERSION_WAIT          4U
#define BT_INIT_ADDRESS_SEND          5U
#define BT_INIT_ADDRESS_WAIT          6U
#define BT_INIT_FEATURES_SEND         7U
#define BT_INIT_FEATURES_WAIT         8U
#define BT_INIT_READY                 9U
#define BT_INIT_ERROR                 10U

typedef struct
{
    uint8_t EventInAddr;
    uint16_t EventInMPS;
    uint8_t EventInterval;
    uint8_t ACLInAddr;
    uint16_t ACLInMPS;
    uint8_t ACLOutAddr;
    uint16_t ACLOutMPS;
    uint8_t EndpointsReady;
    uint8_t EventInPipe;
    uint8_t ACLInPipe;
    uint8_t ACLOutPipe;
    uint8_t PipesReady;
} BT_EndpointInfo_t;

typedef struct
{
    uint8_t HCI_Version;
    uint16_t HCI_Revision;
    uint8_t LMP_Version;
    uint16_t Manufacturer;
    uint16_t LMP_Subversion;
} BT_VersionInfo_t;

extern USBH_ClassTypeDef USBH_Bluetooth_Class;
extern volatile uint8_t BT_InterfaceFound;
extern volatile BT_EndpointInfo_t BT_Endpoints;

/* USB event reception */
extern volatile uint8_t BT_EventRxState;
extern volatile uint32_t BT_EventSubmitCount;
extern volatile uint32_t BT_EventPacketCount;
extern volatile uint32_t BT_EventNAKCount;
extern volatile uint32_t BT_EventErrorCount;
extern volatile uint16_t BT_LastEventLength;
extern volatile uint8_t BT_LastEventData[BT_EVENT_BUFFER_SIZE];

/* HCI Reset results */
extern volatile uint8_t BT_HCI_ResetState;
extern volatile uint8_t BT_HCI_ResetUSBComplete;
extern volatile uint8_t BT_HCI_ResetEventSeen;
extern volatile uint8_t BT_HCI_ResetStatus;

/* Local Version results */
extern volatile BT_VersionInfo_t BT_VersionInfo;
extern volatile uint8_t BT_VersionState;
extern volatile uint8_t BT_VersionUSBComplete;
extern volatile uint8_t BT_VersionEventSeen;
extern volatile uint8_t BT_VersionStatus;

/* Bluetooth address results */
extern volatile uint8_t BT_AddrState;
extern volatile uint8_t BT_AddrUSBComplete;
extern volatile uint8_t BT_AddrEventSeen;
extern volatile uint8_t BT_AddrStatus;
extern volatile uint8_t BT_BD_ADDR[6];

/* Unified initialization */
extern volatile uint8_t BT_InitState;
extern volatile uint16_t BT_InitErrorOpcode;
extern volatile uint8_t BT_LocalFeatures[8];

/* Generic HCI command engine */
extern volatile uint8_t BT_HCI_CmdState;
extern volatile uint8_t BT_HCI_CmdEventSeen;
extern volatile uint8_t BT_HCI_CmdStatus;
extern volatile uint16_t BT_HCI_CmdOpcode;
extern volatile uint8_t BT_HCI_ResponseLength;
extern volatile uint8_t BT_HCI_Response[BT_HCI_MAX_RESPONSE];

USBH_StatusTypeDef BT_HCI_SendCommand(uint16_t opcode, const uint8_t *params, uint8_t length);

/* Scan configuration states */
#define BT_SCAN_WAIT_READY      0U
#define BT_SCAN_WRITE_SEND      1U
#define BT_SCAN_WRITE_WAIT      2U
#define BT_SCAN_READ_SEND       3U
#define BT_SCAN_READ_WAIT       4U
#define BT_SCAN_READY           5U
#define BT_SCAN_ERROR           6U
#define BT_SCAN_UNSUPPORTED     7U

extern volatile uint8_t BT_ScanState;
extern volatile uint8_t BT_ScanMode;
extern volatile uint8_t BT_ScanStatus;

/* Asynchronous HCI events */
#define BT_HCI_EVT_CONNECTION_COMPLETE  0x03U
#define BT_HCI_EVT_CONNECTION_REQUEST   0x04U
#define BT_HCI_EVT_DISCONNECTION        0x05U
#define BT_HCI_EVT_COMMAND_COMPLETE     0x0EU
#define BT_HCI_EVT_COMMAND_STATUS       0x0FU


#define BT_DEVICE_NAME            "Resistivity_Meter"
#define BT_LOCAL_NAME_BYTES       248U

#define BT_NAME_WAIT_READY        0U
#define BT_NAME_SEND              1U
#define BT_NAME_WAIT             2U
#define BT_NAME_OK               3U
#define BT_NAME_ERROR            4U

extern volatile uint8_t BT_NameState;
extern volatile uint8_t BT_NameStatus;

extern volatile uint32_t BT_AsyncEventCount;
extern volatile uint32_t BT_ConnectionRequestCount;
extern volatile uint32_t BT_CommandStatusCount;

extern volatile uint8_t BT_AsyncLastCode;
extern volatile uint8_t BT_AsyncLastLength;
extern volatile uint8_t BT_AsyncLastData[BT_EVENT_BUFFER_SIZE];

extern volatile uint8_t BT_RequesterBD_ADDR[6];
extern volatile uint8_t BT_RequesterLinkType;
extern volatile uint32_t BT_RequesterClassOfDevice;

extern volatile uint8_t BT_LastCommandStatus;
extern volatile uint16_t BT_LastCommandStatusOpcode;

#define BT_HCI_EVT_INQUIRY_COMPLETE 0x01U

extern volatile uint8_t BT_HCI_CmdReplyEvent;
extern volatile uint8_t BT_InquiryCompleteSeen;
extern volatile uint8_t BT_InquiryCompleteStatus;

#endif /* USBH_BLUETOOTH_H */
