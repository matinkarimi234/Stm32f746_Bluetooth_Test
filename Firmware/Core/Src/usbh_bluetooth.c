
#include "usbh_bluetooth.h"
#include "usbh_pipes.h"
#include "usbh_ioreq.h"
#include "usbh_ctlreq.h"
#include "bt_bonds.h"
#include "bt_spp.h"
#include "bt_acl_diag.h"
#include "bt_acl_tx.h"

/* ------------------------------------------------------------- */
/* USB interface and endpoint information                        */
/* ------------------------------------------------------------- */

volatile uint8_t BT_InterfaceFound = 0U;
volatile BT_EndpointInfo_t BT_Endpoints = {0};

/* ------------------------------------------------------------- */
/* USB reception and HCI reassembly                              */
/* ------------------------------------------------------------- */

static uint8_t BT_EventUSBPacket[BT_EVENT_USB_PACKET_SIZE];
static uint8_t BT_EventRxBuffer[BT_EVENT_BUFFER_SIZE];

static uint32_t BT_EventLastTick = 0U;
static uint32_t BT_HCI_PartialTick = 0U;

volatile uint8_t BT_EventRxState = BT_EVENT_RX_SUBMIT;
volatile uint32_t BT_EventSubmitCount = 0U;
volatile uint32_t BT_EventPacketCount = 0U;
volatile uint32_t BT_EventNAKCount = 0U;
volatile uint32_t BT_EventErrorCount = 0U;

volatile uint16_t BT_LastEventLength = 0U;
volatile uint8_t BT_LastEventData[BT_EVENT_BUFFER_SIZE];

volatile uint16_t BT_HCI_PartialLength = 0U;
volatile uint16_t BT_HCI_ExpectedLength = 0U;
volatile uint16_t BT_HCI_LastCompleteLength = 0U;
volatile uint8_t BT_HCI_LastCompleteCode = 0U;

volatile uint32_t BT_HCI_CompleteEventCount = 0U;
volatile uint32_t BT_HCI_AssemblyErrorCount = 0U;
volatile uint32_t BT_HCI_AssemblyTimeoutCount = 0U;

/* ------------------------------------------------------------- */
/* HCI Reset results                                             */
/* ------------------------------------------------------------- */

volatile uint8_t BT_HCI_ResetState = BT_HCI_RESET_IDLE;
volatile uint8_t BT_HCI_ResetUSBComplete = 0U;
volatile uint8_t BT_HCI_ResetEventSeen = 0U;
volatile uint8_t BT_HCI_ResetStatus = 0xFFU;

/* ------------------------------------------------------------- */
/* Read Local Version results                                    */
/* ------------------------------------------------------------- */

volatile BT_VersionInfo_t BT_VersionInfo = {0};
volatile uint8_t BT_VersionState = BT_VERSION_WAIT_RESET;
volatile uint8_t BT_VersionUSBComplete = 0U;
volatile uint8_t BT_VersionEventSeen = 0U;
volatile uint8_t BT_VersionStatus = 0xFFU;

/* ------------------------------------------------------------- */
/* Read BD_ADDR results                                          */
/* ------------------------------------------------------------- */

volatile uint8_t BT_AddrState = BT_ADDR_WAIT_VERSION;
volatile uint8_t BT_AddrUSBComplete = 0U;
volatile uint8_t BT_AddrEventSeen = 0U;
volatile uint8_t BT_AddrStatus = 0xFFU;
volatile uint8_t BT_BD_ADDR[6] = {0};

/* ------------------------------------------------------------- */
/* Generic HCI command engine                                    */
/* ------------------------------------------------------------- */

static uint8_t BT_HCI_CmdBuffer[3U + BT_HCI_MAX_CMD_PARAMS];
static uint8_t BT_HCI_CmdLength = 0U;
static uint32_t BT_HCI_CmdStartTick = 0U;
/* Keep completion through the next full pass, including initialization (which
 * runs before this command engine). Earlier owners cannot overwrite replies. */
static uint8_t BT_HCI_ResultHeld = 0U;

volatile uint8_t BT_HCI_CmdState = BT_HCI_CMD_IDLE;
volatile uint8_t BT_HCI_CmdEventSeen = 0U;
volatile uint8_t BT_HCI_CmdStatus = 0xFFU;
volatile uint16_t BT_HCI_CmdOpcode = 0U;
volatile uint8_t BT_HCI_ResponseLength = 0U;
volatile uint8_t BT_HCI_Response[BT_HCI_MAX_RESPONSE];
volatile uint8_t BT_HCI_CmdReplyEvent = 0U;

/* ------------------------------------------------------------- */
/* Initialization and supported features                         */
/* ------------------------------------------------------------- */

volatile uint8_t BT_InitState = BT_INIT_IDLE;
volatile uint16_t BT_InitErrorOpcode = 0U;
volatile uint8_t BT_LocalFeatures[8] = {0};

/* ------------------------------------------------------------- */
/* Bluetooth local name                                         */
/* ------------------------------------------------------------- */

static const uint8_t BT_LocalNamePayload[BT_LOCAL_NAME_BYTES] = BT_DEVICE_NAME;

volatile uint8_t BT_NameState = BT_NAME_WAIT_READY;
volatile uint8_t BT_NameStatus = 0xFFU;

/* ------------------------------------------------------------- */
/* Scan configuration                                            */
/* ------------------------------------------------------------- */

static const uint8_t BT_DesiredScanMode = 0x03U;

volatile uint8_t BT_ScanState = BT_SCAN_WAIT_READY;
volatile uint8_t BT_ScanMode = 0xFFU;
volatile uint8_t BT_ScanStatus = 0xFFU;


/* Secure Simple Pairing configuration */

volatile uint8_t BT_SSPState = BT_SSP_WAIT_NAME;
volatile uint8_t BT_SSPStatus = 0xFFU;
volatile uint8_t BT_SSPMode = 0xFFU;

static const uint8_t BT_SSPEnableParameter = 0x01U;

/* HCI event mask, little-endian, enabling BR/EDR security events */
static const uint8_t BT_SSPEventMask[8] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x07U, 0xF8U, 0x3FU, 0x00U};

/* SSP pairing management */

volatile uint8_t BT_PairCmdState = BT_PAIR_IDLE;
volatile uint8_t BT_PairCompleteSeen = 0U;
volatile uint8_t BT_PairCompleteStatus = 0xFFU;
volatile uint8_t BT_PairLinkKeyValid = 0U;
volatile uint8_t BT_PairLinkKeyType = 0xFFU;
volatile uint8_t BT_PairLastCommandStatus = 0xFFU;

volatile uint16_t BT_PairLastOpcode = 0U;
volatile uint32_t BT_PairEventCount = 0U;
volatile uint32_t BT_PairReplyCount = 0U;
volatile uint32_t BT_PairErrorCount = 0U;
volatile uint32_t BT_PairDroppedCount = 0U;

/* Pairing reply parameters */
static uint8_t BT_PairCmdParams[22];
static uint8_t BT_PairCmdLength = 0U;
static uint16_t BT_PairCmdOpcode = 0U;
#define BT_PAIR_QUEUE_SIZE 4U
typedef struct { uint16_t opcode; uint8_t length, params[22]; } BT_PairReply;
static BT_PairReply BT_PairQueue[BT_PAIR_QUEUE_SIZE];
static uint8_t BT_PairQueueHead, BT_PairQueueTail, BT_PairQueueCount;

/* RAM-only bond storage; never print or expose the key */
static uint8_t BT_PairLinkKey[16];
static uint8_t BT_PairBondAddress[6];



/* ------------------------------------------------------------- */
/* Asynchronous event information                                */
/* ------------------------------------------------------------- */

volatile uint32_t BT_AsyncEventCount = 0U;
volatile uint32_t BT_ConnectionRequestCount = 0U;
volatile uint32_t BT_CommandStatusCount = 0U;

volatile uint8_t BT_AsyncLastCode = 0U;
volatile uint16_t BT_AsyncLastLength = 0U;
volatile uint8_t BT_AsyncLastData[BT_EVENT_BUFFER_SIZE];

volatile uint8_t BT_RequesterBD_ADDR[6];
volatile uint8_t BT_RequesterLinkType = 0xFFU;
volatile uint32_t BT_RequesterClassOfDevice = 0U;

volatile uint8_t BT_LastCommandStatus = 0xFFU;
volatile uint16_t BT_LastCommandStatusOpcode = 0U;

volatile uint8_t BT_InquiryCompleteSeen = 0U;
volatile uint8_t BT_InquiryCompleteStatus = 0xFFU;


/* Bluetooth connection management */

volatile uint8_t BT_ConnState = BT_CONN_IDLE;
volatile uint8_t BT_Connected = 0U;

volatile uint8_t BT_ConnAcceptStatus = 0xFFU;
volatile uint8_t BT_ConnCompleteSeen = 0U;
volatile uint8_t BT_ConnCompleteStatus = 0xFFU;

volatile uint16_t BT_ConnectionHandle = 0xFFFFU;
volatile uint8_t BT_RemoteBD_ADDR[6] = {0};

volatile uint8_t BT_ConnectionLinkType = 0xFFU;
volatile uint8_t BT_ConnectionEncrypted = 0U;
volatile uint8_t BT_DisconnectReason = 0xFFU;

volatile uint32_t BT_ConnectionCount = 0U;
volatile uint32_t BT_DisconnectionCount = 0U;

static uint8_t BT_ConnAcceptParams[7] = {0};
static uint32_t BT_ConnStartTick = 0U;


/* ------------------------------------------------------------- */
/* USB debugging variables                                       */
/* ------------------------------------------------------------- */

volatile uint8_t BT_HCI_DebugFailStage = 0U;
volatile uint8_t BT_HCI_DebugUSBStatus = 0xFFU;
volatile uint8_t BT_HCI_DebugControlState = 0xFFU;
volatile uint8_t BT_HCI_DebugUSBCompleted = 0U;
volatile uint8_t BT_DebugOutURB = 0xFFU;
volatile uint32_t BT_DebugOutURBCount[6] = {0U};



volatile uint8_t BT_AuthState = BT_AUTH_IDLE;
volatile uint8_t BT_AuthCmdStatus = 0xFFU;
volatile uint8_t BT_AuthCompleteSeen = 0U;
volatile uint8_t BT_AuthCompleteStatus = 0xFFU;
volatile uint32_t BT_AuthCompleteCount = 0U;

static uint32_t BT_AuthStartTick = 0U;
static uint8_t BT_AuthParams[2];


/* ------------------------------------------------------------- */
/* Function declarations                                         */
/* ------------------------------------------------------------- */

static USBH_StatusTypeDef BT_Init(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_DeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_Request(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_SOF(USBH_HandleTypeDef *phost);

static USBH_StatusTypeDef BT_HCI_QueueCommand(uint16_t opcode, const uint8_t *params, uint8_t length);
static void BT_ProcessHCICommand(USBH_HandleTypeDef *phost);
static void BT_ProcessInitialization(void);
static void BT_ProcessScanConfiguration(void);
static void BT_ProcessLocalName(void);

static void BT_DecodeHCIEvent(uint32_t length);
static void BT_DecodeHCICommandStatus(uint32_t length);
static void BT_ProcessAsyncEvent(uint32_t length);
static void BT_AppendHCIEventBytes(const uint8_t *data, uint16_t length);

/* ------------------------------------------------------------- */
/* Bluetooth USB Host class registration                         */
/* ------------------------------------------------------------- */

USBH_ClassTypeDef USBH_Bluetooth_Class = {"BLUETOOTH", USB_BT_CLASS, BT_Init, BT_DeInit, BT_Request, BT_Process, BT_SOF, NULL};

/* ------------------------------------------------------------- */
/* Reset functions                                               */
/* ------------------------------------------------------------- */

static void BT_ClearEndpoints(void)
{
    BT_Endpoints.EventInAddr = 0U;
    BT_Endpoints.EventInMPS = 0U;
    BT_Endpoints.EventInterval = 0U;
    BT_Endpoints.ACLInAddr = 0U;
    BT_Endpoints.ACLInMPS = 0U;
    BT_Endpoints.ACLOutAddr = 0U;
    BT_Endpoints.ACLOutMPS = 0U;
    BT_Endpoints.EndpointsReady = 0U;
    BT_Endpoints.EventInPipe = 0xFFU;
    BT_Endpoints.ACLInPipe = 0xFFU;
    BT_Endpoints.ACLOutPipe = 0xFFU;
    BT_Endpoints.PipesReady = 0U;
}

static void BT_ResetEventRx(void)
{
    uint16_t i;

    BT_EventRxState = BT_EVENT_RX_SUBMIT;
    BT_EventSubmitCount = 0U;
    BT_EventPacketCount = 0U;
    BT_EventNAKCount = 0U;
    BT_EventErrorCount = 0U;
    BT_LastEventLength = 0U;
    BT_EventLastTick = 0U;

    BT_HCI_PartialLength = 0U;
    BT_HCI_ExpectedLength = 0U;
    BT_HCI_LastCompleteLength = 0U;
    BT_HCI_LastCompleteCode = 0U;
    BT_HCI_CompleteEventCount = 0U;
    BT_HCI_AssemblyErrorCount = 0U;
    BT_HCI_AssemblyTimeoutCount = 0U;
    BT_HCI_PartialTick = 0U;

    for (i = 0U; i < BT_EVENT_BUFFER_SIZE; i++) { BT_EventRxBuffer[i] = 0U; BT_LastEventData[i] = 0U; }
    for (i = 0U; i < BT_EVENT_USB_PACKET_SIZE; i++) { BT_EventUSBPacket[i] = 0U; }
}

static void BT_ResetAsyncEvents(void)
{
    uint16_t i;

    BT_AsyncEventCount = 0U;
    BT_ConnectionRequestCount = 0U;
    BT_CommandStatusCount = 0U;
    BT_AsyncLastCode = 0U;
    BT_AsyncLastLength = 0U;
    BT_RequesterLinkType = 0xFFU;
    BT_RequesterClassOfDevice = 0U;
    BT_LastCommandStatus = 0xFFU;
    BT_LastCommandStatusOpcode = 0U;

    for (i = 0U; i < BT_EVENT_BUFFER_SIZE; i++) { BT_AsyncLastData[i] = 0U; }
    for (i = 0U; i < 6U; i++) { BT_RequesterBD_ADDR[i] = 0U; }
}

static void BT_ResetHCIState(void)
{
    BT_HCI_ResetState = BT_HCI_RESET_IDLE;
    BT_HCI_ResetUSBComplete = 0U;
    BT_HCI_ResetEventSeen = 0U;
    BT_HCI_ResetStatus = 0xFFU;
}

static void BT_ResetVersionState(void)
{
    BT_VersionState = BT_VERSION_WAIT_RESET;
    BT_VersionUSBComplete = 0U;
    BT_VersionEventSeen = 0U;
    BT_VersionStatus = 0xFFU;

    BT_VersionInfo.HCI_Version = 0U;
    BT_VersionInfo.HCI_Revision = 0U;
    BT_VersionInfo.LMP_Version = 0U;
    BT_VersionInfo.Manufacturer = 0U;
    BT_VersionInfo.LMP_Subversion = 0U;
}

static void BT_ResetAddressState(void)
{
    uint8_t i;

    BT_AddrState = BT_ADDR_WAIT_VERSION;
    BT_AddrUSBComplete = 0U;
    BT_AddrEventSeen = 0U;
    BT_AddrStatus = 0xFFU;

    for (i = 0U; i < 6U; i++) { BT_BD_ADDR[i] = 0U; }
}

static void BT_ResetGenericCommand(void)
{
    uint16_t i;

    BT_HCI_CmdState = BT_HCI_CMD_IDLE;
    BT_HCI_CmdEventSeen = 0U;
    BT_HCI_CmdStatus = 0xFFU;
    BT_HCI_CmdOpcode = 0U;
    BT_HCI_ResponseLength = 0U;
    BT_HCI_CmdReplyEvent = 0U;
    BT_HCI_CmdLength = 0U;
    BT_HCI_CmdStartTick = 0U;
    BT_HCI_ResultHeld = 0U;

    BT_InitState = BT_INIT_IDLE;
    BT_InitErrorOpcode = 0U;

    BT_NameState = BT_NAME_WAIT_READY;
    BT_NameStatus = 0xFFU;

    BT_ScanState = BT_SCAN_WAIT_READY;
    BT_ScanMode = 0xFFU;
    BT_ScanStatus = 0xFFU;

    BT_InquiryCompleteSeen = 0U;
    BT_InquiryCompleteStatus = 0xFFU;

    BT_HCI_DebugFailStage = 0U;
    BT_HCI_DebugUSBStatus = 0xFFU;
    BT_HCI_DebugControlState = 0xFFU;
    BT_HCI_DebugUSBCompleted = 0U;
    BT_DebugOutURB = 0xFFU;
		
		BT_SSPState = BT_SSP_WAIT_NAME;
		BT_SSPStatus = 0xFFU;
		BT_SSPMode = 0xFFU;

    for (i = 0U; i < 8U; i++) { BT_LocalFeatures[i] = 0U; }
    for (i = 0U; i < sizeof(BT_HCI_CmdBuffer); i++) { BT_HCI_CmdBuffer[i] = 0U; }
    for (i = 0U; i < BT_HCI_MAX_RESPONSE; i++) { BT_HCI_Response[i] = 0U; }
    for (i = 0U; i < 6U; i++) { BT_DebugOutURBCount[i] = 0U; }
}


static void BT_ResetAuthentication(void)
{
    BT_AuthState = BT_AUTH_IDLE;
    BT_AuthCmdStatus = 0xFFU;
    BT_AuthCompleteSeen = 0U;
    BT_AuthCompleteStatus = 0xFFU;
    BT_AuthCompleteCount = 0U;
    BT_AuthStartTick = 0U;
    BT_AuthParams[0] = 0U;
    BT_AuthParams[1] = 0U;
}

/* ------------------------------------------------------------- */
/* USB pipe management                                           */
/* ------------------------------------------------------------- */

static void BT_ReleasePipes(USBH_HandleTypeDef *phost, uint8_t close_pipes)
{
    if (BT_Endpoints.EventInPipe != 0xFFU)
    {
        if (close_pipes != 0U) { USBH_ClosePipe(phost, BT_Endpoints.EventInPipe); }
        USBH_FreePipe(phost, BT_Endpoints.EventInPipe);
        BT_Endpoints.EventInPipe = 0xFFU;
    }

    if (BT_Endpoints.ACLInPipe != 0xFFU)
    {
        if (close_pipes != 0U) { USBH_ClosePipe(phost, BT_Endpoints.ACLInPipe); }
        USBH_FreePipe(phost, BT_Endpoints.ACLInPipe);
        BT_Endpoints.ACLInPipe = 0xFFU;
    }

    if (BT_Endpoints.ACLOutPipe != 0xFFU)
    {
        if (close_pipes != 0U) { USBH_ClosePipe(phost, BT_Endpoints.ACLOutPipe); }
        USBH_FreePipe(phost, BT_Endpoints.ACLOutPipe);
        BT_Endpoints.ACLOutPipe = 0xFFU;
    }

    BT_Endpoints.PipesReady = 0U;
}


static void BT_ResetConnectionState(void)
{
    uint8_t i;

    BT_ConnState = BT_CONN_IDLE;
    BT_Connected = 0U;

    BT_ConnAcceptStatus = 0xFFU;
    BT_ConnCompleteSeen = 0U;
    BT_ConnCompleteStatus = 0xFFU;

    BT_ConnectionHandle = 0xFFFFU;
    BT_ConnectionLinkType = 0xFFU;
    BT_ConnectionEncrypted = 0U;
    BT_DisconnectReason = 0xFFU;

    BT_ConnectionCount = 0U;
    BT_DisconnectionCount = 0U;
    BT_ConnStartTick = 0U;

    for (i = 0U; i < 6U; i++) { BT_RemoteBD_ADDR[i] = 0U; }
    for (i = 0U; i < 7U; i++) { BT_ConnAcceptParams[i] = 0U; }
}


static void BT_ResetPairing(void)
{
    uint8_t i;

    BT_PairCmdState = BT_PAIR_IDLE;
    BT_PairCompleteSeen = 0U;
    BT_PairCompleteStatus = 0xFFU;
    BT_PairLinkKeyValid = 0U;
    BT_PairLinkKeyType = 0xFFU;
    BT_PairLastCommandStatus = 0xFFU;
    BT_PairLastOpcode = 0U;

    BT_PairEventCount = 0U;
    BT_PairReplyCount = 0U;
    BT_PairErrorCount = 0U;
    BT_PairDroppedCount = 0U;

    BT_PairCmdLength = 0U;
    BT_PairCmdOpcode = 0U;
    BT_PairQueueHead = BT_PairQueueTail = BT_PairQueueCount = 0U;

    for (i = 0U; i < 22U; i++) { BT_PairCmdParams[i] = 0U; }
    for (i = 0U; i < 16U; i++) { BT_PairLinkKey[i] = 0U; }
    for (i = 0U; i < 6U; i++) { BT_PairBondAddress[i] = 0U; }
}



/* ------------------------------------------------------------- */
/* USB Bluetooth class initialization                            */
/* ------------------------------------------------------------- */

static USBH_StatusTypeDef BT_Init(USBH_HandleTypeDef *phost)
{
    uint8_t interface;
    uint8_t i;
    uint8_t count;
    uint8_t addr;
    uint8_t type;
    uint16_t mps;

    USBH_InterfaceDescTypeDef *itf;
    USBH_EpDescTypeDef *ep;

    BT_InterfaceFound = 0U;
    BT_ClearEndpoints();
    BT_ResetEventRx();
    BT_ResetAsyncEvents();
    BT_ResetHCIState();
    BT_ResetVersionState();
    BT_ResetAddressState();
    BT_ResetGenericCommand();
		BT_ResetConnectionState();
		BT_ResetPairing();
    BT_Bonds_Init();
		BT_ResetAuthentication();
		BT_ACL_Reset();
		BT_ACL_TX_Reset();

    interface = USBH_FindInterface(phost, USB_BT_CLASS, USB_BT_SUBCLASS, USB_BT_PROTOCOL);

    if (interface == 0xFFU) { return USBH_FAIL; }
    if (USBH_SelectInterface(phost, interface) != USBH_OK) { return USBH_FAIL; }

    BT_InterfaceFound = 1U;
    itf = &phost->device.CfgDesc.Itf_Desc[interface];
    count = itf->bNumEndpoints;

    if (count > USBH_MAX_NUM_ENDPOINTS) { count = USBH_MAX_NUM_ENDPOINTS; }

    for (i = 0U; i < count; i++)
    {
        ep = &itf->Ep_Desc[i];
        addr = ep->bEndpointAddress;
        type = (uint8_t)(ep->bmAttributes & 0x03U);
        mps = (uint16_t)(ep->wMaxPacketSize & 0x07FFU);

        if ((type == USB_EP_TYPE_INTR) && ((addr & 0x80U) != 0U))
        {
            BT_Endpoints.EventInAddr = addr;
            BT_Endpoints.EventInMPS = mps;
            BT_Endpoints.EventInterval = ep->bInterval;
        }
        else if ((type == USB_EP_TYPE_BULK) && ((addr & 0x80U) != 0U))
        {
            BT_Endpoints.ACLInAddr = addr;
            BT_Endpoints.ACLInMPS = mps;
        }
        else if ((type == USB_EP_TYPE_BULK) && ((addr & 0x80U) == 0U))
        {
            BT_Endpoints.ACLOutAddr = addr;
            BT_Endpoints.ACLOutMPS = mps;
        }
    }

    if ((BT_Endpoints.EventInAddr == 0U) || (BT_Endpoints.ACLInAddr == 0U) || (BT_Endpoints.ACLOutAddr == 0U)) { BT_InterfaceFound = 0U; return USBH_FAIL; }
    if ((BT_Endpoints.EventInMPS == 0U) || (BT_Endpoints.EventInMPS > BT_EVENT_USB_PACKET_SIZE) || (BT_Endpoints.ACLInMPS == 0U) || (BT_Endpoints.ACLOutMPS == 0U)) { BT_InterfaceFound = 0U; return USBH_FAIL; }

    BT_Endpoints.EndpointsReady = 1U;

    BT_Endpoints.EventInPipe = USBH_AllocPipe(phost, BT_Endpoints.EventInAddr);
    BT_Endpoints.ACLInPipe = USBH_AllocPipe(phost, BT_Endpoints.ACLInAddr);
    BT_Endpoints.ACLOutPipe = USBH_AllocPipe(phost, BT_Endpoints.ACLOutAddr);

    if ((BT_Endpoints.EventInPipe == 0xFFU) || (BT_Endpoints.ACLInPipe == 0xFFU) || (BT_Endpoints.ACLOutPipe == 0xFFU))
    {
        BT_ReleasePipes(phost, 0U);
        BT_InterfaceFound = 0U;
        return USBH_FAIL;
    }

    if (USBH_OpenPipe(phost, BT_Endpoints.EventInPipe, BT_Endpoints.EventInAddr, phost->device.address, phost->device.speed, USB_EP_TYPE_INTR, BT_Endpoints.EventInMPS) != USBH_OK) { goto pipe_fail; }
    if (USBH_OpenPipe(phost, BT_Endpoints.ACLInPipe, BT_Endpoints.ACLInAddr, phost->device.address, phost->device.speed, USB_EP_TYPE_BULK, BT_Endpoints.ACLInMPS) != USBH_OK) { goto pipe_fail; }
    if (USBH_OpenPipe(phost, BT_Endpoints.ACLOutPipe, BT_Endpoints.ACLOutAddr, phost->device.address, phost->device.speed, USB_EP_TYPE_BULK, BT_Endpoints.ACLOutMPS) != USBH_OK) { goto pipe_fail; }

    USBH_LL_SetToggle(phost, BT_Endpoints.EventInPipe, 0U);
    USBH_LL_SetToggle(phost, BT_Endpoints.ACLInPipe, 0U);
    USBH_LL_SetToggle(phost, BT_Endpoints.ACLOutPipe, 0U);

    BT_Endpoints.PipesReady = 1U;
    BT_InitState = BT_INIT_RESET_SEND;

    return USBH_OK;

pipe_fail:
    BT_ReleasePipes(phost, 1U);
    BT_InterfaceFound = 0U;
    return USBH_FAIL;
}

/* ------------------------------------------------------------- */
/* USB Bluetooth class callbacks                                 */
/* ------------------------------------------------------------- */

static USBH_StatusTypeDef BT_DeInit(USBH_HandleTypeDef *phost)
{
    BT_ReleasePipes(phost, BT_Endpoints.PipesReady);

    BT_InterfaceFound = 0U;
    BT_ClearEndpoints();
    BT_ResetEventRx();
    BT_ResetAsyncEvents();
    BT_ResetHCIState();
    BT_ResetVersionState();
    BT_ResetAddressState();
    BT_ResetGenericCommand();
		BT_ResetConnectionState();
		BT_ResetPairing();
		BT_ResetAuthentication();
		BT_ACL_Reset();
		BT_ACL_TX_Reset();

    return USBH_OK;
}

static USBH_StatusTypeDef BT_Request(USBH_HandleTypeDef *phost)
{
    (void)phost;
    return USBH_OK;
}

/* ------------------------------------------------------------- */
/* Generic HCI command queue                                     */
/* ------------------------------------------------------------- */

static USBH_StatusTypeDef BT_HCI_QueueCommand(uint16_t opcode, const uint8_t *params, uint8_t length)
{
    uint16_t i;

    if (BT_Endpoints.PipesReady == 0U) { return USBH_FAIL; }
    if (length > BT_HCI_MAX_CMD_PARAMS) { return USBH_FAIL; }
    if ((length != 0U) && (params == NULL)) { return USBH_FAIL; }
    if (BT_HCI_ResultHeld != 0U) { return USBH_BUSY; }
    if (BT_HCI_CmdState == BT_HCI_CMD_ERROR) { return USBH_FAIL; }
    if ((BT_HCI_CmdState != BT_HCI_CMD_IDLE) && (BT_HCI_CmdState != BT_HCI_CMD_OK)) { return USBH_BUSY; }

    BT_HCI_CmdBuffer[0] = (uint8_t)(opcode & 0xFFU);
    BT_HCI_CmdBuffer[1] = (uint8_t)((opcode >> 8) & 0xFFU);
    BT_HCI_CmdBuffer[2] = length;

    for (i = 0U; i < length; i++) { BT_HCI_CmdBuffer[3U + i] = params[i]; }

    BT_HCI_CmdLength = (uint8_t)(3U + length);
    BT_HCI_CmdOpcode = opcode;
    BT_HCI_CmdEventSeen = 0U;
    BT_HCI_CmdStatus = 0xFFU;
    BT_HCI_ResponseLength = 0U;
    BT_HCI_CmdReplyEvent = 0U;
    BT_HCI_CmdState = BT_HCI_CMD_SEND;

    BT_HCI_DebugFailStage = 0U;
    BT_HCI_DebugUSBStatus = 0xFFU;
    BT_HCI_DebugControlState = 0xFFU;
    BT_HCI_DebugUSBCompleted = 0U;

    return USBH_OK;
}

USBH_StatusTypeDef BT_HCI_SendCommand(uint16_t opcode, const uint8_t *params, uint8_t length)
{
    if (BT_InitState != BT_INIT_READY) { return USBH_BUSY; }
    return BT_HCI_QueueCommand(opcode, params, length);
}

/* ------------------------------------------------------------- */
/* Generic HCI command processing                                */
/* ------------------------------------------------------------- */

static void BT_ProcessHCICommand(USBH_HandleTypeDef *phost)
{
    USBH_StatusTypeDef status;

    switch (BT_HCI_CmdState)
    {
        case BT_HCI_CMD_IDLE:
            break;

        case BT_HCI_CMD_SEND:
            if (phost->RequestState != CMD_SEND) { break; }

            phost->Control.setup.b.bmRequestType = 0x20U;
            phost->Control.setup.b.bRequest = 0x00U;
            phost->Control.setup.b.wValue.w = 0U;
            phost->Control.setup.b.wIndex.w = 0U;
            phost->Control.setup.b.wLength.w = BT_HCI_CmdLength;

            BT_HCI_CmdStartTick = phost->Timer;
            BT_HCI_CmdState = BT_HCI_CMD_USB_WAIT;
            break;

        case BT_HCI_CMD_USB_WAIT:
            if (phost->Control.state == CTRL_DATA_OUT_WAIT)
            {
                uint8_t urb = (uint8_t)USBH_LL_GetURBState(phost, phost->Control.pipe_out);
                BT_DebugOutURB = urb;
                if (urb < 6U) { BT_DebugOutURBCount[urb]++; }
            }

            status = USBH_CtlReq(phost, BT_HCI_CmdBuffer, BT_HCI_CmdLength);

            BT_HCI_DebugUSBStatus = (uint8_t)status;
            BT_HCI_DebugControlState = (uint8_t)phost->Control.state;

            if (status == USBH_OK)
            {
                BT_HCI_DebugUSBCompleted = 1U;

                if (BT_HCI_CmdOpcode == 0x0C03U) { BT_HCI_ResetUSBComplete = 1U; BT_HCI_ResetState = BT_HCI_RESET_EVENT_WAIT; }
                if (BT_HCI_CmdOpcode == 0x1001U) { BT_VersionUSBComplete = 1U; BT_VersionState = BT_VERSION_EVENT_WAIT; }
                if (BT_HCI_CmdOpcode == 0x1009U) { BT_AddrUSBComplete = 1U; BT_AddrState = BT_ADDR_EVENT_WAIT; }

                BT_HCI_CmdStartTick = phost->Timer;
                BT_HCI_CmdState = BT_HCI_CMD_EVENT_WAIT;
            }
            else if (status != USBH_BUSY)
            {
                BT_HCI_DebugFailStage = 1U;
                BT_HCI_CmdState = BT_HCI_CMD_ERROR;
            }
            else if ((phost->Timer - BT_HCI_CmdStartTick) >= 2000U)
            {
                BT_HCI_DebugFailStage = 2U;
                BT_HCI_CmdState = BT_HCI_CMD_ERROR;
            }
            break;

        case BT_HCI_CMD_EVENT_WAIT:
            if (BT_HCI_CmdEventSeen != 0U)
            {
                if (BT_HCI_CmdStatus == 0U)
                {
                    BT_HCI_CmdState = BT_HCI_CMD_OK;
                    BT_HCI_ResultHeld = 2U;
                }
                else
                {
                    BT_HCI_DebugFailStage = 4U;
                    BT_HCI_CmdState = BT_HCI_CMD_ERROR;
                    BT_HCI_ResultHeld = 2U;
                }
            }
            else if ((phost->Timer - BT_HCI_CmdStartTick) >= 2000U)
            {
                BT_HCI_DebugFailStage = 3U;
                BT_HCI_CmdState = BT_HCI_CMD_ERROR;
            }
            break;

        case BT_HCI_CMD_OK:
        case BT_HCI_CMD_ERROR:
            break;

        default:
            BT_HCI_CmdState = BT_HCI_CMD_ERROR;
            break;
    }
}

/* ------------------------------------------------------------- */
/* Automatic initialization                                      */
/* ------------------------------------------------------------- */

static void BT_InitFailed(uint16_t opcode)
{
    BT_InitErrorOpcode = opcode;
    BT_InitState = BT_INIT_ERROR;

    if (opcode == 0x0C03U) { BT_HCI_ResetState = BT_HCI_RESET_ERROR; }
    if (opcode == 0x1001U) { BT_VersionState = BT_VERSION_ERROR; }
    if (opcode == 0x1009U) { BT_AddrState = BT_ADDR_ERROR; }
}

static void BT_ProcessInitialization(void)
{
    USBH_StatusTypeDef status;

    switch (BT_InitState)
    {
        case BT_INIT_RESET_SEND:
            status = BT_HCI_QueueCommand(0x0C03U, NULL, 0U);
            if (status == USBH_OK) { BT_HCI_ResetState = BT_HCI_RESET_USB_WAIT; BT_InitState = BT_INIT_RESET_WAIT; }
            else if (status == USBH_FAIL) { BT_InitFailed(0x0C03U); }
            break;

        case BT_INIT_RESET_WAIT:
            if (BT_HCI_CmdState == BT_HCI_CMD_OK) { BT_HCI_ResetState = BT_HCI_RESET_OK; BT_InitState = BT_INIT_VERSION_SEND; }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR) { BT_InitFailed(0x0C03U); }
            break;

        case BT_INIT_VERSION_SEND:
            status = BT_HCI_QueueCommand(0x1001U, NULL, 0U);
            if (status == USBH_OK) { BT_VersionState = BT_VERSION_USB_WAIT; BT_InitState = BT_INIT_VERSION_WAIT; }
            else if (status == USBH_FAIL) { BT_InitFailed(0x1001U); }
            break;

        case BT_INIT_VERSION_WAIT:
            if (BT_HCI_CmdState == BT_HCI_CMD_OK) { BT_VersionState = BT_VERSION_OK; BT_InitState = BT_INIT_ADDRESS_SEND; }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR) { BT_InitFailed(0x1001U); }
            break;

        case BT_INIT_ADDRESS_SEND:
            status = BT_HCI_QueueCommand(0x1009U, NULL, 0U);
            if (status == USBH_OK) { BT_AddrState = BT_ADDR_USB_WAIT; BT_InitState = BT_INIT_ADDRESS_WAIT; }
            else if (status == USBH_FAIL) { BT_InitFailed(0x1009U); }
            break;

        case BT_INIT_ADDRESS_WAIT:
            if (BT_HCI_CmdState == BT_HCI_CMD_OK) { BT_AddrState = BT_ADDR_OK; BT_InitState = BT_INIT_FEATURES_SEND; }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR) { BT_InitFailed(0x1009U); }
            break;

        case BT_INIT_FEATURES_SEND:
            status = BT_HCI_QueueCommand(0x1003U, NULL, 0U);
            if (status == USBH_OK) { BT_InitState = BT_INIT_FEATURES_WAIT; }
            else if (status == USBH_FAIL) { BT_InitFailed(0x1003U); }
            break;

        case BT_INIT_FEATURES_WAIT:
            if (BT_HCI_CmdState == BT_HCI_CMD_OK) { BT_InitState = BT_INIT_READY; }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR) { BT_InitFailed(0x1003U); }
            break;

        case BT_INIT_IDLE:
        case BT_INIT_READY:
        case BT_INIT_ERROR:
        default:
            break;
    }
}

/* ------------------------------------------------------------- */
/* HCI Command Complete decoder                                  */
/* ------------------------------------------------------------- */

static void BT_DecodeHCIEvent(uint32_t length)
{
    uint32_t j;
    uint16_t opcode;

    if ((length < 6U) || (length > BT_HCI_MAX_RESPONSE)) { return; }
    if ((BT_EventRxBuffer[0] != BT_HCI_EVT_COMMAND_COMPLETE) || (((uint32_t)BT_EventRxBuffer[1] + 2U) != length)) { return; }
    if ((BT_HCI_CmdState != BT_HCI_CMD_USB_WAIT) && (BT_HCI_CmdState != BT_HCI_CMD_EVENT_WAIT)) { return; }

    opcode = (uint16_t)BT_EventRxBuffer[3] | ((uint16_t)BT_EventRxBuffer[4] << 8);

    if (opcode != BT_HCI_CmdOpcode) { return; }

    BT_HCI_CmdStatus = BT_EventRxBuffer[5];
    BT_HCI_ResponseLength = (uint8_t)length;

    for (j = 0U; j < length; j++) { BT_HCI_Response[j] = BT_EventRxBuffer[j]; }

    if (opcode == 0x0C03U)
    {
        BT_HCI_ResetStatus = BT_HCI_CmdStatus;
        BT_HCI_ResetEventSeen = 1U;
    }
    else if (opcode == 0x1001U)
    {
        BT_VersionStatus = BT_HCI_CmdStatus;

        if (BT_VersionStatus == 0U)
        {
            if ((length == 14U) && (BT_EventRxBuffer[1] == 12U))
            {
                BT_VersionInfo.HCI_Version = BT_EventRxBuffer[6];
                BT_VersionInfo.HCI_Revision = (uint16_t)BT_EventRxBuffer[7] | ((uint16_t)BT_EventRxBuffer[8] << 8);
                BT_VersionInfo.LMP_Version = BT_EventRxBuffer[9];
                BT_VersionInfo.Manufacturer = (uint16_t)BT_EventRxBuffer[10] | ((uint16_t)BT_EventRxBuffer[11] << 8);
                BT_VersionInfo.LMP_Subversion = (uint16_t)BT_EventRxBuffer[12] | ((uint16_t)BT_EventRxBuffer[13] << 8);
            }
            else
            {
                BT_VersionStatus = 0xFFU;
                BT_HCI_CmdStatus = 0xFFU;
            }
        }

        BT_VersionEventSeen = 1U;
    }
    else if (opcode == 0x1009U)
    {
        BT_AddrStatus = BT_HCI_CmdStatus;

        if (BT_AddrStatus == 0U)
        {
            if ((length == 12U) && (BT_EventRxBuffer[1] == 10U))
            {
                BT_BD_ADDR[0] = BT_EventRxBuffer[11];
                BT_BD_ADDR[1] = BT_EventRxBuffer[10];
                BT_BD_ADDR[2] = BT_EventRxBuffer[9];
                BT_BD_ADDR[3] = BT_EventRxBuffer[8];
                BT_BD_ADDR[4] = BT_EventRxBuffer[7];
                BT_BD_ADDR[5] = BT_EventRxBuffer[6];
            }
            else
            {
                BT_AddrStatus = 0xFFU;
                BT_HCI_CmdStatus = 0xFFU;
            }
        }

        BT_AddrEventSeen = 1U;
    }
    else if (opcode == 0x1003U)
    {
        if ((BT_HCI_CmdStatus == 0U) && (length == 14U) && (BT_EventRxBuffer[1] == 12U))
        {
            for (j = 0U; j < 8U; j++) { BT_LocalFeatures[j] = BT_EventRxBuffer[6U + j]; }
        }
        else if (BT_HCI_CmdStatus == 0U)
        {
            BT_HCI_CmdStatus = 0xFFU;
        }
    }

    BT_HCI_CmdReplyEvent = BT_HCI_EVT_COMMAND_COMPLETE;
    BT_HCI_CmdEventSeen = 1U;
}

/* ------------------------------------------------------------- */
/* HCI Command Status decoder                                    */
/* ------------------------------------------------------------- */

static void BT_DecodeHCICommandStatus(uint32_t length)
{
    uint16_t opcode;
    uint8_t i;

    if (length != 6U) { return; }
    if (BT_EventRxBuffer[0] != BT_HCI_EVT_COMMAND_STATUS) { return; }
    if (BT_EventRxBuffer[1] != 4U) { return; }
    if ((BT_HCI_CmdState != BT_HCI_CMD_USB_WAIT) && (BT_HCI_CmdState != BT_HCI_CMD_EVENT_WAIT)) { return; }

    opcode = (uint16_t)BT_EventRxBuffer[4] | ((uint16_t)BT_EventRxBuffer[5] << 8);

    if (opcode != BT_HCI_CmdOpcode) { return; }

    BT_HCI_CmdStatus = BT_EventRxBuffer[2];
    BT_HCI_CmdReplyEvent = BT_HCI_EVT_COMMAND_STATUS;
    BT_HCI_ResponseLength = (uint8_t)length;

    for (i = 0U; i < length; i++) { BT_HCI_Response[i] = BT_EventRxBuffer[i]; }

    BT_HCI_CmdEventSeen = 1U;
}

/* ------------------------------------------------------------- */
/* Local Bluetooth name configuration                            */
/* ------------------------------------------------------------- */

static void BT_ProcessLocalName(void)
{
    USBH_StatusTypeDef status;

    switch (BT_NameState)
    {
        case BT_NAME_WAIT_READY:
            if (BT_InitState == BT_INIT_READY) { BT_NameState = BT_NAME_SEND; }
            else if (BT_InitState == BT_INIT_ERROR) { BT_NameState = BT_NAME_ERROR; }
            break;

        case BT_NAME_SEND:
            status = BT_HCI_SendCommand(0x0C13U, BT_LocalNamePayload, BT_LOCAL_NAME_BYTES);
            if (status == USBH_OK) { BT_NameState = BT_NAME_WAIT; }
            else if (status == USBH_FAIL) { BT_NameState = BT_NAME_ERROR; }
            break;

        case BT_NAME_WAIT:
            if (BT_HCI_CmdState == BT_HCI_CMD_OK)
            {
                BT_NameStatus = BT_HCI_CmdStatus;

                if ((BT_HCI_CmdOpcode == 0x0C13U) && (BT_HCI_ResponseLength == 6U) && (BT_HCI_Response[0] == 0x0EU) && (BT_HCI_Response[1] == 0x04U) && (BT_HCI_Response[3] == 0x13U) && (BT_HCI_Response[4] == 0x0CU) && (BT_NameStatus == 0U))
                {
                    BT_NameState = BT_NAME_OK;
                }
                else
                {
                    BT_NameState = BT_NAME_ERROR;
                }
            }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR)
            {
                BT_NameStatus = BT_HCI_CmdStatus;
                BT_NameState = BT_NAME_ERROR;
            }
            break;

        case BT_NAME_OK:
        case BT_NAME_ERROR:
        default:
            break;
    }
}

/* ------------------------------------------------------------- */
/* Bluetooth scan configuration                                  */
/* ------------------------------------------------------------- */

static void BT_ProcessScanConfiguration(void)
{
    USBH_StatusTypeDef status;

    switch (BT_ScanState)
    {
        case BT_SCAN_WAIT_READY:
            if (BT_InitState != BT_INIT_READY) { break; }
            if (BT_NameState == BT_NAME_ERROR) { BT_ScanState = BT_SCAN_ERROR; break; }
            if (BT_NameState != BT_NAME_OK) { break; }
						if (BT_SSPState == BT_SSP_ERROR) { BT_ScanState = BT_SCAN_ERROR; break; }
						if (BT_SSPState == BT_SSP_UNSUPPORTED) { BT_ScanState = BT_SCAN_UNSUPPORTED; break; }
						if (BT_SSPState != BT_SSP_READY) { break; }

            if ((BT_LocalFeatures[4] & 0x20U) != 0U)
            {
                BT_ScanState = BT_SCAN_UNSUPPORTED;
                break;
            }

            BT_ScanState = BT_SCAN_WRITE_SEND;
            break;

        case BT_SCAN_WRITE_SEND:
            status = BT_HCI_SendCommand(0x0C1AU, &BT_DesiredScanMode, 1U);
            if (status == USBH_OK) { BT_ScanState = BT_SCAN_WRITE_WAIT; }
            else if (status == USBH_FAIL) { BT_ScanState = BT_SCAN_ERROR; }
            break;

        case BT_SCAN_WRITE_WAIT:
            if (BT_HCI_CmdState == BT_HCI_CMD_OK)
            {
                BT_ScanStatus = BT_HCI_CmdStatus;
                BT_ScanState = BT_SCAN_READ_SEND;
            }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR)
            {
                BT_ScanStatus = BT_HCI_CmdStatus;
                BT_ScanState = BT_SCAN_ERROR;
            }
            break;

        case BT_SCAN_READ_SEND:
            status = BT_HCI_SendCommand(0x0C19U, NULL, 0U);
            if (status == USBH_OK) { BT_ScanState = BT_SCAN_READ_WAIT; }
            else if (status == USBH_FAIL) { BT_ScanState = BT_SCAN_ERROR; }
            break;

        case BT_SCAN_READ_WAIT:
            if (BT_HCI_CmdState == BT_HCI_CMD_OK)
            {
                BT_ScanStatus = BT_HCI_CmdStatus;

                if ((BT_HCI_ResponseLength == 7U) && (BT_HCI_Response[0] == 0x0EU) && (BT_HCI_Response[3] == 0x19U) && (BT_HCI_Response[4] == 0x0CU))
                {
                    BT_ScanMode = BT_HCI_Response[6];
                    BT_ScanState = (BT_ScanMode == BT_DesiredScanMode) ? BT_SCAN_READY : BT_SCAN_ERROR;
                }
                else
                {
                    BT_ScanState = BT_SCAN_ERROR;
                }
            }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR)
            {
                BT_ScanStatus = BT_HCI_CmdStatus;
                BT_ScanState = BT_SCAN_ERROR;
            }
            break;

        case BT_SCAN_READY:
        case BT_SCAN_ERROR:
        case BT_SCAN_UNSUPPORTED:
        default:
            break;
    }
}


static uint8_t BT_PairPeerMatches(const uint8_t *hci_address)
{
    uint8_t i;

    if ((BT_ConnState != BT_CONN_ACCEPT_WAIT) && (BT_ConnState != BT_CONN_WAIT_COMPLETE) && (BT_ConnState != BT_CONN_CONNECTED)) { return 0U; }

    for (i = 0U; i < 6U; i++)
    {
        if (hci_address[i] != BT_RequesterBD_ADDR[5U - i]) { return 0U; }
    }

    return 1U;
}



static uint8_t BT_QueuePairReply(uint16_t opcode, const uint8_t *address, const uint8_t *extra, uint8_t extra_length)
{
    uint8_t i;

    if ((address == NULL) || (extra_length > 16U) || ((extra_length != 0U) && (extra == NULL))) { return 0U; }

    if (BT_PairQueueCount >= BT_PAIR_QUEUE_SIZE)
    { BT_PairDroppedCount++; return 0U; }
    BT_PairQueue[BT_PairQueueTail].opcode = opcode;
    BT_PairQueue[BT_PairQueueTail].length = (uint8_t)(6U + extra_length);
    for (i = 0U; i < 6U; i++) { BT_PairQueue[BT_PairQueueTail].params[i] = address[i]; }
    for (i = 0U; i < extra_length; i++) { BT_PairQueue[BT_PairQueueTail].params[6U+i] = extra[i]; }
    BT_PairQueueTail = (uint8_t)((BT_PairQueueTail + 1U) % BT_PAIR_QUEUE_SIZE);
    BT_PairQueueCount++;

    return 1U;
}


/* ------------------------------------------------------------- */
/* Asynchronous HCI event decoder                                */
/* ------------------------------------------------------------- */

static void BT_ProcessAsyncEvent(uint32_t length)
{
    uint8_t event_code;
    uint32_t i;

    if ((length < 2U) || (length > BT_EVENT_BUFFER_SIZE)) { return; }
    if (((uint32_t)BT_EventRxBuffer[1] + 2U) != length) { return; }

    event_code = BT_EventRxBuffer[0];

    if (event_code == BT_HCI_EVT_COMMAND_COMPLETE) { return; }

    /* Track peer-initiated security, even when auto-auth is disabled. */
    if ((event_code == 0x08U) && (length == 6U) && (BT_EventRxBuffer[2] == 0U) &&
        ((((uint16_t)BT_EventRxBuffer[3] | ((uint16_t)BT_EventRxBuffer[4] << 8)) & 0x0FFFU) == BT_ConnectionHandle))
    { BT_ConnectionEncrypted = BT_EventRxBuffer[5]; }
    BT_AsyncEventCount++;
    BT_AsyncLastCode = event_code;
    BT_AsyncLastLength = (uint16_t)length;

    for (i = 0U; i < length; i++) { BT_AsyncLastData[i] = ((event_code == 0x18U) && (i >= 8U) && (i < 24U)) ? 0U : BT_EventRxBuffer[i]; }

    switch (event_code)
    {
        case BT_HCI_EVT_COMMAND_STATUS:
            if (length == 6U)
            {
                BT_CommandStatusCount++;
                BT_LastCommandStatus = BT_EventRxBuffer[2];
                BT_LastCommandStatusOpcode = (uint16_t)BT_EventRxBuffer[4] | ((uint16_t)BT_EventRxBuffer[5] << 8);
            }
            break;

				case BT_HCI_EVT_CONNECTION_REQUEST:

						if ((length == 12U) && (BT_EventRxBuffer[1] == 10U))
						{
								BT_ConnectionRequestCount++;

								/* Store requester information for debugging */
								BT_RequesterClassOfDevice = (uint32_t)BT_EventRxBuffer[8] | ((uint32_t)BT_EventRxBuffer[9] << 8) | ((uint32_t)BT_EventRxBuffer[10] << 16);
								BT_RequesterLinkType = BT_EventRxBuffer[11];

								/* Accept only ACL requests in this first implementation */
								if ((BT_AUTO_ACCEPT_CONNECTIONS == 1U) && (BT_ScanState == BT_SCAN_READY) && ((BT_ConnState == BT_CONN_IDLE) || (BT_ConnState == BT_CONN_FAILED)) && (BT_RequesterLinkType == 0x01U))
								{
										for (i = 0U; i < 6U; i++)
										{
												BT_RequesterBD_ADDR[i] = BT_EventRxBuffer[7U - i];
												BT_ConnAcceptParams[i] = BT_EventRxBuffer[2U + i];
										}

										/* 0x01 = remain peripheral, no role switch */
										BT_ConnAcceptParams[6] = 0x01U;

										BT_ConnAcceptStatus = 0xFFU;
										BT_ConnCompleteStatus = 0xFFU;
										BT_ConnCompleteSeen = 0U;

										BT_ConnState = BT_CONN_ACCEPT_SEND;
								}
						}

						break;

        case BT_HCI_EVT_INQUIRY_COMPLETE:
            if ((length == 3U) && (BT_EventRxBuffer[1] == 1U))
            {
                BT_InquiryCompleteStatus = BT_EventRxBuffer[2];
                BT_InquiryCompleteSeen = 1U;
            }
            break;
						
				
				
				case BT_HCI_EVT_LINK_KEY_REQUEST:
				{
						uint8_t key_matches = BT_PairLinkKeyValid;

						if (length != 8U) { break; }

                        /* HCI event addresses are in byte-reversed display order. */
                        if (BT_Bonds_Find(&BT_EventRxBuffer[2], BT_PairLinkKey) != 0U)
                        {
                            for (i = 0U; i < 6U; ++i) { BT_PairBondAddress[i] = BT_EventRxBuffer[2U + i]; }
                            BT_PairLinkKeyValid = 1U;
                            key_matches = 1U;
                        }

						BT_PairEventCount++;

						for (i = 0U; i < 6U; i++)
						{
								if (BT_PairBondAddress[i] != BT_EventRxBuffer[2U + i]) { key_matches = 0U; }
						}

						if ((BT_PairPeerMatches(&BT_EventRxBuffer[2]) != 0U) && (key_matches != 0U))
						{
								BT_QueuePairReply(BT_HCI_LINK_KEY_REPLY, &BT_EventRxBuffer[2], BT_PairLinkKey, 16U);
						}
						else
						{
								BT_QueuePairReply(BT_HCI_LINK_KEY_NEG_REPLY, &BT_EventRxBuffer[2], NULL, 0U);
						}

						break;
				}
				
				
				case BT_HCI_EVT_IO_CAP_REQUEST:
				{
						static const uint8_t io_params[3] = {0x03U, 0x00U, 0x04U};
						static const uint8_t reject_reason = 0x0FU;

						if (length != 8U) { break; }

						BT_PairEventCount++;

						if ((BT_PAIRING_TEST_MODE == 1U) && (BT_PairPeerMatches(&BT_EventRxBuffer[2]) != 0U))
						{
								BT_QueuePairReply(BT_HCI_IO_CAP_REPLY, &BT_EventRxBuffer[2], io_params, 3U);
						}
						else
						{
								BT_QueuePairReply(BT_HCI_IO_CAP_NEG_REPLY, &BT_EventRxBuffer[2], &reject_reason, 1U);
						}

						break;
				}


				case BT_HCI_EVT_USER_CONFIRM_REQUEST:

						if (length != 12U) { break; }

						BT_PairEventCount++;

						if ((BT_PAIRING_TEST_MODE == 1U) && (BT_PairPeerMatches(&BT_EventRxBuffer[2]) != 0U))
						{
								BT_QueuePairReply(BT_HCI_USER_CONFIRM_REPLY, &BT_EventRxBuffer[2], NULL, 0U);
						}
						else
						{
								BT_QueuePairReply(BT_HCI_USER_CONFIRM_NEG_REPLY, &BT_EventRxBuffer[2], NULL, 0U);
						}

						break;
						
						

				case BT_HCI_EVT_PIN_CODE_REQUEST:

						if (length == 8U)
						{
								BT_PairEventCount++;
								BT_QueuePairReply(BT_HCI_PIN_CODE_NEG_REPLY, &BT_EventRxBuffer[2], NULL, 0U);
						}

						break;

				case BT_HCI_EVT_USER_PASSKEY_REQUEST:

						if (length == 8U)
						{
								BT_PairEventCount++;
								BT_QueuePairReply(BT_HCI_PASSKEY_NEG_REPLY, &BT_EventRxBuffer[2], NULL, 0U);
						}

						break;

				case BT_HCI_EVT_REMOTE_OOB_REQUEST:

						if (length == 8U)
						{
								BT_PairEventCount++;
								BT_QueuePairReply(BT_HCI_REMOTE_OOB_NEG_REPLY, &BT_EventRxBuffer[2], NULL, 0U);
						}

						break;
						

				case BT_HCI_EVT_SIMPLE_PAIR_COMPLETE:

						if ((length == 9U) && (BT_PairPeerMatches(&BT_EventRxBuffer[3]) != 0U))
						{
								BT_PairEventCount++;
								BT_PairCompleteStatus = BT_EventRxBuffer[2];
								BT_PairCompleteSeen = 1U;

								if (BT_PairCompleteStatus != 0U) { BT_PairErrorCount++; }
						}

						break;
				
				case BT_HCI_EVT_AUTH_COMPLETE:

						if ((length == 5U) && (BT_EventRxBuffer[1] == 3U))
						{
								uint16_t handle = ((uint16_t)BT_EventRxBuffer[3] | ((uint16_t)BT_EventRxBuffer[4] << 8)) & 0x0FFFU;

								if (handle == BT_ConnectionHandle)
								{
										BT_AuthCompleteStatus = BT_EventRxBuffer[2];
										BT_AuthCompleteSeen = 1U;
										BT_AuthCompleteCount++;
                                        BT_AuthState = (BT_AuthCompleteStatus == 0U) ? BT_AUTH_OK : BT_AUTH_ERROR;
								}
						}

						break;

				case BT_HCI_EVT_LINK_KEY_NOTIFICATION:

						if ((length == 25U) && (BT_PAIRING_TEST_MODE == 1U) && (BT_PairPeerMatches(&BT_EventRxBuffer[2]) != 0U))
						{
								uint8_t key_type = BT_EventRxBuffer[24];

								BT_PairEventCount++;

								if ((key_type == 0x04U) || (key_type == 0x05U) || (key_type == 0x07U) || (key_type == 0x08U))
								{
										for (i = 0U; i < 6U; i++) { BT_PairBondAddress[i] = BT_EventRxBuffer[2U + i]; }
										for (i = 0U; i < 16U; i++) { BT_PairLinkKey[i] = BT_EventRxBuffer[8U + i]; }

										BT_PairLinkKeyType = key_type;
										BT_PairLinkKeyValid = 1U;
                                        BT_Bonds_Remember(&BT_EventRxBuffer[2], &BT_EventRxBuffer[8], key_type);
								}
								else
								{
										BT_PairLinkKeyValid = 0U;
										BT_PairErrorCount++;
								}
						}

						break;





				case BT_HCI_EVT_CONNECTION_COMPLETE:

						if ((length == 13U) && (BT_EventRxBuffer[1] == 11U))
						{
								uint8_t address_matches = 1U;

								for (i = 0U; i < 6U; i++)
								{
										if (BT_EventRxBuffer[10U - i] != BT_RequesterBD_ADDR[i]) { address_matches = 0U; }
								}

								if (address_matches == 0U) { break; }

								BT_ConnCompleteStatus = BT_EventRxBuffer[2];
								BT_ConnCompleteSeen = 1U;
								
								if ((BT_ConnState != BT_CONN_ACCEPT_WAIT) && (BT_ConnState != BT_CONN_WAIT_COMPLETE)) { break; }
								
								if (BT_ConnCompleteStatus == 0U)
								{
										BT_ConnectionHandle = ((uint16_t)BT_EventRxBuffer[3] | ((uint16_t)BT_EventRxBuffer[4] << 8)) & 0x0FFFU;
										BT_ConnectionLinkType = BT_EventRxBuffer[11];
										BT_ConnectionEncrypted = BT_EventRxBuffer[12];

										for (i = 0U; i < 6U; i++) { BT_RemoteBD_ADDR[i] = BT_EventRxBuffer[10U - i]; }
								}
						}

						break;



			case BT_HCI_EVT_DISCONNECTION:

					if ((length == 6U) && (BT_EventRxBuffer[1] == 4U))
					{
							uint16_t disconnected_handle = ((uint16_t)BT_EventRxBuffer[3] | ((uint16_t)BT_EventRxBuffer[4] << 8)) & 0x0FFFU;

							if ((BT_EventRxBuffer[2] == 0U) && (BT_Connected != 0U) && (disconnected_handle == BT_ConnectionHandle))
							{
									BT_DisconnectReason = BT_EventRxBuffer[5];
									BT_DisconnectionCount++;

									BT_Connected = 0U;
                                    BT_AuthCompleteSeen = 0U;
                                    BT_AuthState = BT_AUTH_IDLE;
                                    BT_SPP_Reset();
                                    BT_ACL_TX_LinkClosed();
                                    BT_ACL_LinkClosed();
									BT_ConnectionHandle = 0xFFFFU;
									BT_ConnectionEncrypted = 0U;
									BT_ConnCompleteSeen = 0U;
									BT_ConnState = BT_CONN_IDLE;
							}
					}

					break;


        default:
            break;
    }
}

/* ------------------------------------------------------------- */
/* Application event callback                                    */
/* ------------------------------------------------------------- */

__weak void BT_HCI_EventCallback(const uint8_t *event, uint16_t length)
{
    (void)event;
    (void)length;
}

/* ------------------------------------------------------------- */
/* HCI event reassembly                                          */
/* ------------------------------------------------------------- */

static void BT_AppendHCIEventBytes(const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint16_t complete_length;

    for (i = 0U; i < length; i++)
    {
        /* Buffer protection */
        if (BT_HCI_PartialLength >= BT_EVENT_BUFFER_SIZE)
        {
            BT_HCI_AssemblyErrorCount++;
            BT_HCI_PartialLength = 0U;
            BT_HCI_ExpectedLength = 0U;
        }

        /* Append received byte */
        BT_EventRxBuffer[BT_HCI_PartialLength] = data[i];
        BT_HCI_PartialLength++;

        /* HCI header received */
        if (BT_HCI_PartialLength == 2U)
        {
            BT_HCI_ExpectedLength = (uint16_t)BT_EventRxBuffer[1] + 2U;

            if (BT_HCI_ExpectedLength > BT_EVENT_BUFFER_SIZE)
            {
                BT_HCI_AssemblyErrorCount++;
                BT_HCI_PartialLength = 0U;
                BT_HCI_ExpectedLength = 0U;
                continue;
            }
        }

        /* A complete HCI event is available */
        if ((BT_HCI_ExpectedLength != 0U) && (BT_HCI_PartialLength == BT_HCI_ExpectedLength))
        {
            complete_length = BT_HCI_ExpectedLength;

            BT_HCI_CompleteEventCount++;
            BT_HCI_LastCompleteLength = complete_length;
            BT_HCI_LastCompleteCode = BT_EventRxBuffer[0];

            /* Decode the assembled event */
            BT_DecodeHCIEvent(complete_length);
						BT_DecodeHCICommandStatus(complete_length);
						BT_ProcessAsyncEvent(complete_length);
						BT_ACL_TX_OnHCIEvent(BT_EventRxBuffer, complete_length);
						BT_HCI_EventCallback(BT_EventRxBuffer, complete_length);

            /* Reset for the next event */
            BT_HCI_PartialLength = 0U;
            BT_HCI_ExpectedLength = 0U;
        }
    }
}


static void BT_ProcessConnection(USBH_HandleTypeDef *phost)
{
    USBH_StatusTypeDef status;

    switch (BT_ConnState)
    {
        case BT_CONN_IDLE:
            break;

        case BT_CONN_ACCEPT_SEND:

            status = BT_HCI_SendCommand(BT_HCI_ACCEPT_CONNECTION_OPCODE, BT_ConnAcceptParams, 7U);

            if (status == USBH_OK)
            {
                BT_ConnStartTick = phost->Timer;
                BT_ConnState = BT_CONN_ACCEPT_WAIT;
            }
            else if (status == USBH_FAIL)
            {
                BT_ConnState = BT_CONN_FAILED;
            }
            break;

        case BT_CONN_ACCEPT_WAIT:

            if (BT_HCI_CmdState == BT_HCI_CMD_OK)
            {
                BT_ConnAcceptStatus = BT_HCI_CmdStatus;

                if ((BT_HCI_CmdOpcode == BT_HCI_ACCEPT_CONNECTION_OPCODE) && (BT_HCI_CmdReplyEvent == BT_HCI_EVT_COMMAND_STATUS) && (BT_ConnAcceptStatus == 0U))
                {
                    BT_ConnStartTick = phost->Timer;
                    BT_ConnState = BT_CONN_WAIT_COMPLETE;
                }
                else
                {
                    BT_ConnState = BT_CONN_FAILED;
                }
            }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR)
            {
                BT_ConnAcceptStatus = BT_HCI_CmdStatus;
                BT_ConnState = BT_CONN_FAILED;
            }
            break;

        case BT_CONN_WAIT_COMPLETE:

            if (BT_ConnCompleteSeen != 0U)
            {
                if ((BT_ConnCompleteStatus == 0U) && (BT_ConnectionLinkType == 0x01U))
                {
                    BT_Connected = 1U;
                    BT_ConnectionCount++;
                    BT_ConnState = BT_CONN_CONNECTED;
                }
                else
                {
                    BT_Connected = 0U;
                    BT_ConnState = BT_CONN_FAILED;
                }
            }
            else if ((phost->Timer - BT_ConnStartTick) >= BT_CONN_COMPLETE_TIMEOUT_MS)
            {
                BT_ConnCompleteStatus = 0xFEU;
                BT_ConnState = BT_CONN_FAILED;
            }
            break;

        case BT_CONN_CONNECTED:
            break;

        case BT_CONN_FAILED:
            break;

        default:
            BT_ConnState = BT_CONN_FAILED;
            break;
    }
}


static void BT_ProcessSSPConfiguration(void)
{
    USBH_StatusTypeDef status;

    switch (BT_SSPState)
    {
        case BT_SSP_WAIT_NAME:

            if (BT_NameState == BT_NAME_ERROR)
            {
                BT_SSPState = BT_SSP_ERROR;
                break;
            }

            if (BT_NameState != BT_NAME_OK) { break; }

            /* Local supported features, byte 6 bit 3 = SSP support */
            if ((BT_LocalFeatures[6] & 0x08U) == 0U)
            {
                BT_SSPState = BT_SSP_UNSUPPORTED;
                break;
            }

            BT_SSPState = BT_SSP_MASK_SEND;
            break;

        case BT_SSP_MASK_SEND:

            status = BT_HCI_SendCommand(BT_HCI_SET_EVENT_MASK_OPCODE, BT_SSPEventMask, 8U);

            if (status == USBH_OK) { BT_SSPState = BT_SSP_MASK_WAIT; }
            else if (status == USBH_FAIL) { BT_SSPState = BT_SSP_ERROR; }
            break;

        case BT_SSP_MASK_WAIT:

            if (BT_HCI_CmdState == BT_HCI_CMD_OK)
            {
                BT_SSPStatus = BT_HCI_CmdStatus;

                if ((BT_HCI_CmdOpcode == BT_HCI_SET_EVENT_MASK_OPCODE) && (BT_HCI_ResponseLength == 6U) && (BT_HCI_Response[0] == 0x0EU) && (BT_SSPStatus == 0U))
                {
                    BT_SSPState = BT_SSP_ENABLE_SEND;
                }
                else
                {
                    BT_SSPState = BT_SSP_ERROR;
                }
            }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR)
            {
                BT_SSPStatus = BT_HCI_CmdStatus;
                BT_SSPState = BT_SSP_ERROR;
            }
            break;

        case BT_SSP_ENABLE_SEND:

            status = BT_HCI_SendCommand(BT_HCI_WRITE_SSP_OPCODE, &BT_SSPEnableParameter, 1U);

            if (status == USBH_OK) { BT_SSPState = BT_SSP_ENABLE_WAIT; }
            else if (status == USBH_FAIL) { BT_SSPState = BT_SSP_ERROR; }
            break;

        case BT_SSP_ENABLE_WAIT:

            if (BT_HCI_CmdState == BT_HCI_CMD_OK)
            {
                BT_SSPStatus = BT_HCI_CmdStatus;

                if ((BT_HCI_CmdOpcode == BT_HCI_WRITE_SSP_OPCODE) && (BT_HCI_ResponseLength == 6U) && (BT_HCI_Response[0] == 0x0EU) && (BT_SSPStatus == 0U))
                {
                    BT_SSPState = BT_SSP_READ_SEND;
                }
                else
                {
                    BT_SSPState = BT_SSP_ERROR;
                }
            }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR)
            {
                BT_SSPStatus = BT_HCI_CmdStatus;
                BT_SSPState = BT_SSP_ERROR;
            }
            break;

        case BT_SSP_READ_SEND:

            status = BT_HCI_SendCommand(BT_HCI_READ_SSP_OPCODE, NULL, 0U);

            if (status == USBH_OK) { BT_SSPState = BT_SSP_READ_WAIT; }
            else if (status == USBH_FAIL) { BT_SSPState = BT_SSP_ERROR; }
            break;

        case BT_SSP_READ_WAIT:

            if (BT_HCI_CmdState == BT_HCI_CMD_OK)
            {
                BT_SSPStatus = BT_HCI_CmdStatus;

                if ((BT_HCI_CmdOpcode == BT_HCI_READ_SSP_OPCODE) && (BT_HCI_ResponseLength == 7U) && (BT_HCI_Response[0] == 0x0EU) && (BT_HCI_Response[3] == 0x55U) && (BT_HCI_Response[4] == 0x0CU) && (BT_SSPStatus == 0U))
                {
                    BT_SSPMode = BT_HCI_Response[6];
                    BT_SSPState = (BT_SSPMode == 0x01U) ? BT_SSP_READY : BT_SSP_ERROR;
                }
                else
                {
                    BT_SSPState = BT_SSP_ERROR;
                }
            }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR)
            {
                BT_SSPStatus = BT_HCI_CmdStatus;
                BT_SSPState = BT_SSP_ERROR;
            }
            break;

        case BT_SSP_READY:
        case BT_SSP_ERROR:
        case BT_SSP_UNSUPPORTED:
        default:
            break;
    }
}


static void BT_ProcessPairing(void)
{
    USBH_StatusTypeDef status;
    uint8_t i;
    if (BT_PairCmdState == BT_PAIR_IDLE && BT_PairQueueCount != 0U)
    {
        BT_PairCmdOpcode = BT_PairQueue[BT_PairQueueHead].opcode;
        BT_PairCmdLength = BT_PairQueue[BT_PairQueueHead].length;
        for (i = 0U; i < BT_PairCmdLength; ++i)
        {
            BT_PairCmdParams[i] = BT_PairQueue[BT_PairQueueHead].params[i];
            BT_PairQueue[BT_PairQueueHead].params[i] = 0U;
        }
        BT_PairQueueHead = (uint8_t)((BT_PairQueueHead + 1U) % BT_PAIR_QUEUE_SIZE);
        BT_PairQueueCount--;
        BT_PairCmdState = BT_PAIR_SEND;
    }

    switch (BT_PairCmdState)
    {
        case BT_PAIR_IDLE:
            break;

        case BT_PAIR_SEND:

            status = BT_HCI_SendCommand(BT_PairCmdOpcode, BT_PairCmdParams, BT_PairCmdLength);

            if (status == USBH_OK)
            {
                BT_PairLastOpcode = BT_PairCmdOpcode;
                BT_PairCmdState = BT_PAIR_WAIT;
            }
            else if (status == USBH_FAIL)
            {
                BT_PairErrorCount++;
                BT_PairCmdState = BT_PAIR_IDLE;
            }

            break;

        case BT_PAIR_WAIT:

            if (BT_HCI_CmdOpcode != BT_PairCmdOpcode) { break; }

            if (BT_HCI_CmdState == BT_HCI_CMD_OK)
            {
                BT_PairLastCommandStatus = BT_HCI_CmdStatus;

                if ((BT_HCI_CmdReplyEvent == BT_HCI_EVT_COMMAND_COMPLETE) && (BT_HCI_ResponseLength == 12U) && (BT_PairLastCommandStatus == 0U))
                {
                    BT_PairReplyCount++;
                }
                else
                {
                    BT_PairErrorCount++;
                }

                BT_PairCmdState = BT_PAIR_IDLE;
            }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR)
            {
                BT_PairLastCommandStatus = BT_HCI_CmdStatus;
                BT_PairErrorCount++;
                BT_PairCmdState = BT_PAIR_IDLE;
            }

            break;

        default:
            BT_PairCmdState = BT_PAIR_IDLE;
            BT_PairErrorCount++;
            break;
    }
}


static void BT_ProcessAuthentication(USBH_HandleTypeDef *phost)
{
    USBH_StatusTypeDef status;

    if (BT_Connected == 0U)
    {
        BT_AuthState = BT_AUTH_IDLE;
        return;
    }

    switch (BT_AuthState)
    {
        case BT_AUTH_IDLE:

            if ((BT_AUTO_AUTH_TEST == 1U) && (BT_SSPState == BT_SSP_READY) && (BT_ConnectionHandle != 0xFFFFU))
            {
                BT_AuthState = BT_AUTH_SEND;
            }
            break;

        case BT_AUTH_SEND:

            BT_AuthParams[0] = (uint8_t)(BT_ConnectionHandle & 0xFFU);
            BT_AuthParams[1] = (uint8_t)(BT_ConnectionHandle >> 8);

            status = BT_HCI_SendCommand(BT_HCI_AUTH_REQUEST_OPCODE, BT_AuthParams, 2U);

            if (status == USBH_OK)
            {
                BT_AuthStartTick = phost->Timer;
                BT_AuthState = BT_AUTH_CMD_WAIT;
            }
            else if (status == USBH_FAIL)
            {
                BT_AuthState = BT_AUTH_ERROR;
            }
            break;

        case BT_AUTH_CMD_WAIT:

            if (BT_HCI_CmdOpcode != BT_HCI_AUTH_REQUEST_OPCODE) { break; }

            if (BT_HCI_CmdState == BT_HCI_CMD_OK)
            {
                BT_AuthCmdStatus = BT_HCI_CmdStatus;

                if ((BT_HCI_CmdReplyEvent == BT_HCI_EVT_COMMAND_STATUS) && (BT_AuthCmdStatus == 0U))
                {
                    BT_AuthState = BT_AUTH_RESULT_WAIT;
                }
                else
                {
                    BT_AuthState = BT_AUTH_ERROR;
                }
            }
            else if (BT_HCI_CmdState == BT_HCI_CMD_ERROR)
            {
                BT_AuthCmdStatus = BT_HCI_CmdStatus;
                BT_AuthState = BT_AUTH_ERROR;
            }
            break;

        case BT_AUTH_RESULT_WAIT:

            if (BT_AuthCompleteSeen != 0U)
            {
                BT_AuthState = (BT_AuthCompleteStatus == 0U) ? BT_AUTH_OK : BT_AUTH_ERROR;
            }
            else if ((phost->Timer - BT_AuthStartTick) >= BT_AUTH_TIMEOUT_MS)
            {
                BT_AuthCompleteStatus = 0xFEU;
                BT_AuthState = BT_AUTH_ERROR;
            }
            break;

        case BT_AUTH_OK:
        case BT_AUTH_ERROR:
        default:
            break;
    }
}


/* ------------------------------------------------------------- */
/* Main USB Host Bluetooth process                               */
/* ------------------------------------------------------------- */

static USBH_StatusTypeDef BT_Process(USBH_HandleTypeDef *phost)
{
    USBH_URBStateTypeDef urb;
    USBH_StatusTypeDef status;
    uint32_t length;
    uint32_t i;
    uint32_t interval;

    if (BT_Endpoints.PipesReady == 0U) { return USBH_OK; }

    /* Incomplete HCI event timeout */
    if ((BT_HCI_PartialLength != 0U) && ((phost->Timer - BT_HCI_PartialTick) >= 1000U))
    {
        BT_HCI_AssemblyTimeoutCount++;
        BT_HCI_PartialLength = 0U;
        BT_HCI_ExpectedLength = 0U;
    }

    interval = (BT_Endpoints.EventInterval == 0U) ? 1U : BT_Endpoints.EventInterval;

    switch (BT_EventRxState)
    {
        case BT_EVENT_RX_SUBMIT:
            if ((BT_Endpoints.EventInMPS == 0U) || (BT_Endpoints.EventInMPS > BT_EVENT_USB_PACKET_SIZE))
            {
                BT_EventErrorCount++;
                BT_EventRxState = BT_EVENT_RX_ERROR;
                break;
            }

            status = USBH_InterruptReceiveData(phost, BT_EventUSBPacket, (uint8_t)BT_Endpoints.EventInMPS, BT_Endpoints.EventInPipe);

            if (status == USBH_OK)
            {
                BT_EventSubmitCount++;
                BT_EventRxState = BT_EVENT_RX_WAIT;
            }
            else
            {
                BT_EventErrorCount++;
                BT_EventLastTick = phost->Timer;
                BT_EventRxState = BT_EVENT_RX_DELAY;
            }
            break;

        case BT_EVENT_RX_WAIT:
            urb = USBH_LL_GetURBState(phost, BT_Endpoints.EventInPipe);

            if (urb == USBH_URB_DONE)
            {
                length = USBH_LL_GetLastXferSize(phost, BT_Endpoints.EventInPipe);

                if (length > BT_EVENT_USB_PACKET_SIZE)
                {
                    BT_EventErrorCount++;
                    BT_EventRxState = BT_EVENT_RX_ERROR;
                    break;
                }

                /* Preserve last raw USB packet */
                BT_LastEventLength = (uint16_t)length;

                /* Raw USB chunks may contain fragmented link keys: do not publish them. */
                for (i = 0U; i < BT_EVENT_BUFFER_SIZE; i++) { BT_LastEventData[i] = 0U; }

                BT_EventPacketCount++;

                /* Add received bytes to the HCI reassembler */
                if (length != 0U)
                {
                    BT_HCI_PartialTick = phost->Timer;
                    BT_AppendHCIEventBytes(BT_EventUSBPacket, (uint16_t)length);
                }

                BT_EventLastTick = phost->Timer;
                BT_EventRxState = BT_EVENT_RX_DELAY;
            }
            else if ((urb == USBH_URB_NOTREADY) || (urb == USBH_URB_NYET))
            {
                BT_EventNAKCount++;
                BT_EventLastTick = phost->Timer;
                BT_EventRxState = BT_EVENT_RX_DELAY;
            }
            else if ((urb == USBH_URB_ERROR) || (urb == USBH_URB_STALL))
            {
                BT_EventErrorCount++;
                BT_EventRxState = BT_EVENT_RX_ERROR;
            }
            break;

        case BT_EVENT_RX_DELAY:
            if ((phost->Timer - BT_EventLastTick) >= interval) { BT_EventRxState = BT_EVENT_RX_SUBMIT; }
            break;

        case BT_EVENT_RX_ERROR:
            break;

        default:
            BT_EventRxState = BT_EVENT_RX_ERROR;
            break;
    }

    /* Existing Bluetooth initialization and configuration */
		BT_ProcessInitialization();
		BT_ProcessHCICommand(phost);
		BT_ProcessLocalName();
		BT_ProcessSSPConfiguration();
		BT_ProcessScanConfiguration();
		BT_ProcessConnection(phost);
		BT_ProcessAuthentication(phost);
		BT_ProcessPairing();
        /* Ensure TX/controller link is ready before decoding incoming L2CAP. */
        BT_ACL_TX_Process(phost);
        BT_SPP_Process();
        BT_ACL_Process(phost);
        BT_SPP_Process();
        BT_ACL_TX_Process(phost);
        /* A controller rejection is a completed command, not a broken USB pipe.
         * Release only after its owner has had a complete pass to observe it. */
        if (BT_HCI_ResultHeld != 0U)
        {
            BT_HCI_ResultHeld--;
            if ((BT_HCI_ResultHeld == 0U) && (BT_HCI_CmdState == BT_HCI_CMD_ERROR) &&
                BT_HCI_CmdEventSeen && BT_HCI_DebugUSBCompleted)
            { BT_HCI_CmdState = BT_HCI_CMD_IDLE; }
        }
				
		
    return USBH_OK;
}

/* ------------------------------------------------------------- */
/* Start-of-frame callback                                       */
/* ------------------------------------------------------------- */

static USBH_StatusTypeDef BT_SOF(USBH_HandleTypeDef *phost)
{
    (void)phost;
    return USBH_OK;
}
