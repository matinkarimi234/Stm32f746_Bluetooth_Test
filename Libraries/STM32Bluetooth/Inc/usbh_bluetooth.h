
#ifndef USBH_BLUETOOTH_H
#define USBH_BLUETOOTH_H

#include "bt_config.h"
#include "usbh_core.h"

/* USB Bluetooth class */
#define USB_BT_CLASS                       0xE0U
#define USB_BT_SUBCLASS                    0x01U
#define USB_BT_PROTOCOL                    0x01U

/* USB packet and complete HCI event buffers */
#define BT_EVENT_BUFFER_SIZE               257U
#define BT_EVENT_USB_PACKET_SIZE           64U

/* USB interrupt reception states */
#define BT_EVENT_RX_SUBMIT                 0U
#define BT_EVENT_RX_WAIT                   1U
#define BT_EVENT_RX_DELAY                  2U
#define BT_EVENT_RX_ERROR                  3U

/* HCI Reset states */
#define BT_HCI_RESET_IDLE                  0U
#define BT_HCI_RESET_SEND                  1U
#define BT_HCI_RESET_USB_WAIT              2U
#define BT_HCI_RESET_EVENT_WAIT            3U
#define BT_HCI_RESET_OK                    4U
#define BT_HCI_RESET_ERROR                 5U

/* Read Local Version states */
#define BT_VERSION_WAIT_RESET              0U
#define BT_VERSION_SEND                    1U
#define BT_VERSION_USB_WAIT                2U
#define BT_VERSION_EVENT_WAIT              3U
#define BT_VERSION_OK                      4U
#define BT_VERSION_ERROR                   5U

/* Read Bluetooth Address states */
#define BT_ADDR_WAIT_VERSION               0U
#define BT_ADDR_SEND                       1U
#define BT_ADDR_USB_WAIT                   2U
#define BT_ADDR_EVENT_WAIT                 3U
#define BT_ADDR_OK                         4U
#define BT_ADDR_ERROR                      5U

/* Generic HCI command engine */
#define BT_HCI_MAX_CMD_PARAMS              248U
#define BT_HCI_MAX_RESPONSE                64U
#define BT_HCI_CMD_IDLE                    0U
#define BT_HCI_CMD_SEND                    1U
#define BT_HCI_CMD_USB_WAIT                2U
#define BT_HCI_CMD_EVENT_WAIT              3U
#define BT_HCI_CMD_OK                      4U
#define BT_HCI_CMD_ERROR                   5U

/* Bluetooth initialization */
#define BT_INIT_IDLE                       0U
#define BT_INIT_RESET_SEND                 1U
#define BT_INIT_RESET_WAIT                 2U
#define BT_INIT_VERSION_SEND               3U
#define BT_INIT_VERSION_WAIT               4U
#define BT_INIT_ADDRESS_SEND               5U
#define BT_INIT_ADDRESS_WAIT               6U
#define BT_INIT_FEATURES_SEND              7U
#define BT_INIT_FEATURES_WAIT              8U
#define BT_INIT_READY                      9U
#define BT_INIT_ERROR                      10U

/* Scan configuration */
#define BT_SCAN_WAIT_READY                 0U
#define BT_SCAN_WRITE_SEND                 1U
#define BT_SCAN_WRITE_WAIT                 2U
#define BT_SCAN_READ_SEND                  3U
#define BT_SCAN_READ_WAIT                  4U
#define BT_SCAN_READY                      5U
#define BT_SCAN_ERROR                      6U
#define BT_SCAN_UNSUPPORTED                7U

/* Local Bluetooth name */
#define BT_LOCAL_NAME_BYTES                248U
#define BT_NAME_WAIT_READY                 0U
#define BT_NAME_SEND                       1U
#define BT_NAME_WAIT                       2U
#define BT_NAME_OK                         3U
#define BT_NAME_ERROR                      4U

/* HCI asynchronous event codes */
#define BT_HCI_EVT_INQUIRY_COMPLETE        0x01U
#define BT_HCI_EVT_CONNECTION_COMPLETE     0x03U
#define BT_HCI_EVT_CONNECTION_REQUEST      0x04U
#define BT_HCI_EVT_DISCONNECTION           0x05U
#define BT_HCI_EVT_COMMAND_COMPLETE        0x0EU
#define BT_HCI_EVT_COMMAND_STATUS          0x0FU

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

/* USB Host class */
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

/* HCI event reassembly */
extern volatile uint16_t BT_HCI_PartialLength;
extern volatile uint16_t BT_HCI_ExpectedLength;
extern volatile uint16_t BT_HCI_LastCompleteLength;
extern volatile uint8_t BT_HCI_LastCompleteCode;
extern volatile uint32_t BT_HCI_CompleteEventCount;
extern volatile uint32_t BT_HCI_AssemblyErrorCount;
extern volatile uint32_t BT_HCI_AssemblyTimeoutCount;

/* HCI Reset */
extern volatile uint8_t BT_HCI_ResetState;
extern volatile uint8_t BT_HCI_ResetUSBComplete;
extern volatile uint8_t BT_HCI_ResetEventSeen;
extern volatile uint8_t BT_HCI_ResetStatus;

/* Local Version */
extern volatile BT_VersionInfo_t BT_VersionInfo;
extern volatile uint8_t BT_VersionState;
extern volatile uint8_t BT_VersionUSBComplete;
extern volatile uint8_t BT_VersionEventSeen;
extern volatile uint8_t BT_VersionStatus;

/* Bluetooth Address */
extern volatile uint8_t BT_AddrState;
extern volatile uint8_t BT_AddrUSBComplete;
extern volatile uint8_t BT_AddrEventSeen;
extern volatile uint8_t BT_AddrStatus;
extern volatile uint8_t BT_BD_ADDR[6];

/* Controller initialization */
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
extern volatile uint8_t BT_HCI_CmdReplyEvent;

/* Local name configuration */
extern volatile uint8_t BT_NameState;
extern volatile uint8_t BT_NameStatus;

/* Scan configuration */
extern volatile uint8_t BT_ScanState;
extern volatile uint8_t BT_ScanMode;
extern volatile uint8_t BT_ScanStatus;

/* Asynchronous HCI events */
extern volatile uint32_t BT_AsyncEventCount;
extern volatile uint32_t BT_ConnectionRequestCount;
extern volatile uint32_t BT_CommandStatusCount;
extern volatile uint8_t BT_AsyncLastCode;
extern volatile uint16_t BT_AsyncLastLength;
extern volatile uint8_t BT_AsyncLastData[BT_EVENT_BUFFER_SIZE];
extern volatile uint8_t BT_RequesterBD_ADDR[6];
extern volatile uint8_t BT_RequesterLinkType;
extern volatile uint32_t BT_RequesterClassOfDevice;
extern volatile uint8_t BT_LastCommandStatus;
extern volatile uint16_t BT_LastCommandStatusOpcode;

/* Inquiry */
extern volatile uint8_t BT_InquiryCompleteSeen;
extern volatile uint8_t BT_InquiryCompleteStatus;

/* USB diagnostics */
extern volatile uint8_t BT_HCI_DebugFailStage;
extern volatile uint8_t BT_HCI_DebugUSBStatus;
extern volatile uint8_t BT_HCI_DebugControlState;
extern volatile uint8_t BT_HCI_DebugUSBCompleted;
extern volatile uint8_t BT_DebugOutURB;
extern volatile uint32_t BT_DebugOutURBCount[6];

/* Public functions */
USBH_StatusTypeDef BT_HCI_SendCommand(uint16_t opcode, const uint8_t *params, uint8_t length);
void BT_HCI_EventCallback(const uint8_t *event, uint16_t length);


/* Step 15: Bluetooth ACL connection management */
#define BT_HCI_ACCEPT_CONNECTION_OPCODE  0x0409U

#define BT_CONN_IDLE                     0U
#define BT_CONN_ACCEPT_SEND              1U
#define BT_CONN_ACCEPT_WAIT              2U
#define BT_CONN_WAIT_COMPLETE            3U
#define BT_CONN_CONNECTED                4U
#define BT_CONN_FAILED                   5U

#define BT_CONN_COMPLETE_TIMEOUT_MS      30000U

/* Development only: automatically accept incoming ACL requests */

extern volatile uint8_t BT_ConnState;
extern volatile uint8_t BT_Connected;
extern volatile uint8_t BT_ConnAcceptStatus;
extern volatile uint8_t BT_ConnCompleteSeen;
extern volatile uint8_t BT_ConnCompleteStatus;
extern volatile uint16_t BT_ConnectionHandle;
extern volatile uint8_t BT_RemoteBD_ADDR[6];
extern volatile uint8_t BT_ConnectionLinkType;
extern volatile uint8_t BT_ConnectionEncrypted;
extern volatile uint8_t BT_DisconnectReason;
extern volatile uint32_t BT_ConnectionCount;
extern volatile uint32_t BT_DisconnectionCount;


/* Step 16A: Secure Simple Pairing configuration */

#define BT_HCI_SET_EVENT_MASK_OPCODE     0x0C01U
#define BT_HCI_WRITE_SSP_OPCODE          0x0C56U
#define BT_HCI_READ_SSP_OPCODE           0x0C55U

#define BT_SSP_WAIT_NAME                 0U
#define BT_SSP_MASK_SEND                 1U
#define BT_SSP_MASK_WAIT                 2U
#define BT_SSP_ENABLE_SEND               3U
#define BT_SSP_ENABLE_WAIT               4U
#define BT_SSP_READ_SEND                 5U
#define BT_SSP_READ_WAIT                 6U
#define BT_SSP_READY                     7U
#define BT_SSP_ERROR                     8U
#define BT_SSP_UNSUPPORTED               9U

extern volatile uint8_t BT_SSPState;
extern volatile uint8_t BT_SSPStatus;
extern volatile uint8_t BT_SSPMode;



/* Step 16B: Bluetooth SSP pairing */

/* Development-only automatic Just Works confirmation */

/* HCI pairing events */
#define BT_HCI_EVT_PIN_CODE_REQUEST      0x16U
#define BT_HCI_EVT_LINK_KEY_REQUEST      0x17U
#define BT_HCI_EVT_LINK_KEY_NOTIFICATION 0x18U
#define BT_HCI_EVT_IO_CAP_REQUEST        0x31U
#define BT_HCI_EVT_USER_CONFIRM_REQUEST  0x33U
#define BT_HCI_EVT_USER_PASSKEY_REQUEST  0x34U
#define BT_HCI_EVT_REMOTE_OOB_REQUEST    0x35U
#define BT_HCI_EVT_SIMPLE_PAIR_COMPLETE  0x36U

/* HCI pairing commands */
#define BT_HCI_LINK_KEY_REPLY            0x040BU
#define BT_HCI_LINK_KEY_NEG_REPLY        0x040CU
#define BT_HCI_PIN_CODE_NEG_REPLY        0x040EU
#define BT_HCI_IO_CAP_REPLY              0x042BU
#define BT_HCI_USER_CONFIRM_REPLY        0x042CU
#define BT_HCI_USER_CONFIRM_NEG_REPLY    0x042DU
#define BT_HCI_PASSKEY_NEG_REPLY         0x042FU
#define BT_HCI_REMOTE_OOB_NEG_REPLY      0x0433U
#define BT_HCI_IO_CAP_NEG_REPLY          0x0434U

/* Pairing command states */
#define BT_PAIR_IDLE                     0U
#define BT_PAIR_SEND                     1U
#define BT_PAIR_WAIT                     2U

extern volatile uint8_t BT_PairCmdState;
extern volatile uint8_t BT_PairCompleteSeen;
extern volatile uint8_t BT_PairCompleteStatus;
extern volatile uint8_t BT_PairLinkKeyValid;
extern volatile uint8_t BT_PairLinkKeyType;
extern volatile uint8_t BT_PairLastCommandStatus;

extern volatile uint16_t BT_PairLastOpcode;
extern volatile uint32_t BT_PairEventCount;
extern volatile uint32_t BT_PairReplyCount;
extern volatile uint32_t BT_PairErrorCount;
extern volatile uint32_t BT_PairDroppedCount;


/* Authentication test */
#define BT_HCI_AUTH_REQUEST_OPCODE    0x0411U
#define BT_HCI_EVT_AUTH_COMPLETE      0x06U

#define BT_AUTH_IDLE                  0U
#define BT_AUTH_SEND                  1U
#define BT_AUTH_CMD_WAIT              2U
#define BT_AUTH_RESULT_WAIT           3U
#define BT_AUTH_OK                    4U
#define BT_AUTH_ERROR                 5U

#define BT_AUTH_TIMEOUT_MS            30000U

extern volatile uint8_t BT_AuthState;
extern volatile uint8_t BT_AuthCmdStatus;
extern volatile uint8_t BT_AuthCompleteSeen;
extern volatile uint8_t BT_AuthCompleteStatus;
extern volatile uint32_t BT_AuthCompleteCount;




#endif /* USBH_BLUETOOTH_H */
