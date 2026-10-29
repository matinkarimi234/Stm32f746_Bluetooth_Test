
#include "usbh_bluetooth.h"
#include "usbh_pipes.h"
#include "usbh_ioreq.h"
#include "usbh_ctlreq.h"

/* USB interface and endpoint information */
volatile uint8_t BT_InterfaceFound = 0U;
volatile BT_EndpointInfo_t BT_Endpoints = {0};

/* HCI event reception */
static uint8_t BT_EventRxBuffer[BT_EVENT_BUFFER_SIZE];
static uint32_t BT_EventLastTick = 0U;
volatile uint8_t BT_EventRxState = BT_EVENT_RX_SUBMIT;
volatile uint32_t BT_EventSubmitCount = 0U;
volatile uint32_t BT_EventPacketCount = 0U;
volatile uint32_t BT_EventNAKCount = 0U;
volatile uint32_t BT_EventErrorCount = 0U;
volatile uint16_t BT_LastEventLength = 0U;
volatile uint8_t BT_LastEventData[BT_EVENT_BUFFER_SIZE];

/* Legacy Watch variables retained for compatibility */
volatile uint8_t BT_HCI_ResetState = BT_HCI_RESET_IDLE;
volatile uint8_t BT_HCI_ResetUSBComplete = 0U;
volatile uint8_t BT_HCI_ResetEventSeen = 0U;
volatile uint8_t BT_HCI_ResetStatus = 0xFFU;

/* Read Local Version results */
volatile BT_VersionInfo_t BT_VersionInfo = {0};
volatile uint8_t BT_VersionState = BT_VERSION_WAIT_RESET;
volatile uint8_t BT_VersionUSBComplete = 0U;
volatile uint8_t BT_VersionEventSeen = 0U;
volatile uint8_t BT_VersionStatus = 0xFFU;

/* Read BD_ADDR results */
volatile uint8_t BT_AddrState = BT_ADDR_WAIT_VERSION;
volatile uint8_t BT_AddrUSBComplete = 0U;
volatile uint8_t BT_AddrEventSeen = 0U;
volatile uint8_t BT_AddrStatus = 0xFFU;
volatile uint8_t BT_BD_ADDR[6] = {0};

/* Shared HCI command engine and automatic initialization */
static uint8_t BT_HCI_CmdBuffer[3U + BT_HCI_MAX_CMD_PARAMS];
static uint8_t BT_HCI_CmdLength = 0U;
static uint32_t BT_HCI_CmdStartTick = 0U;

volatile uint8_t BT_InitState = BT_INIT_IDLE;
volatile uint16_t BT_InitErrorOpcode = 0U;
volatile uint8_t BT_LocalFeatures[8] = {0};

volatile uint8_t BT_HCI_CmdState = BT_HCI_CMD_IDLE;
volatile uint8_t BT_HCI_CmdEventSeen = 0U;
volatile uint8_t BT_HCI_CmdStatus = 0xFFU;
volatile uint16_t BT_HCI_CmdOpcode = 0U;
volatile uint8_t BT_HCI_ResponseLength = 0U;
volatile uint8_t BT_HCI_Response[BT_HCI_MAX_RESPONSE];

static USBH_StatusTypeDef BT_Init(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_DeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_Request(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_SOF(USBH_HandleTypeDef *phost);

static USBH_StatusTypeDef BT_HCI_QueueCommand(uint16_t opcode, const uint8_t *params, uint8_t length);
static void BT_ProcessHCICommand(USBH_HandleTypeDef *phost);
static void BT_ProcessInitialization(void);
static void BT_DecodeHCIEvent(uint32_t length);

USBH_ClassTypeDef USBH_Bluetooth_Class = {"BLUETOOTH", USB_BT_CLASS, BT_Init, BT_DeInit, BT_Request, BT_Process, BT_SOF, NULL};

/* ------------------------------------------------------------- */
/* Endpoint and pipe management                                  */
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

    for (i = 0U; i < BT_EVENT_BUFFER_SIZE; i++)
    {
        BT_EventRxBuffer[i] = 0U;
        BT_LastEventData[i] = 0U;
    }
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
    uint8_t i;

    BT_HCI_CmdState = BT_HCI_CMD_IDLE;
    BT_HCI_CmdEventSeen = 0U;
    BT_HCI_CmdStatus = 0xFFU;
    BT_HCI_CmdOpcode = 0U;
    BT_HCI_ResponseLength = 0U;
    BT_HCI_CmdLength = 0U;
    BT_HCI_CmdStartTick = 0U;
    BT_InitState = BT_INIT_IDLE;
    BT_InitErrorOpcode = 0U;

    for (i = 0U; i < 8U; i++) { BT_LocalFeatures[i] = 0U; }
    for (i = 0U; i < sizeof(BT_HCI_CmdBuffer); i++) { BT_HCI_CmdBuffer[i] = 0U; }
    for (i = 0U; i < BT_HCI_MAX_RESPONSE; i++) { BT_HCI_Response[i] = 0U; }
}

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

/* ------------------------------------------------------------- */
/* USB Host Bluetooth class initialization                       */
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
    BT_ResetHCIState();
    BT_ResetVersionState();
    BT_ResetAddressState();
    BT_ResetGenericCommand();

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
    if ((BT_Endpoints.EventInMPS == 0U) || (BT_Endpoints.EventInMPS > BT_EVENT_BUFFER_SIZE) || (BT_Endpoints.ACLInMPS == 0U) || (BT_Endpoints.ACLOutMPS == 0U)) { BT_InterfaceFound = 0U; return USBH_FAIL; }

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
/* USB Host class callbacks                                      */
/* ------------------------------------------------------------- */

static USBH_StatusTypeDef BT_DeInit(USBH_HandleTypeDef *phost)
{
    BT_ReleasePipes(phost, BT_Endpoints.PipesReady);

    BT_InterfaceFound = 0U;
    BT_ClearEndpoints();
    BT_ResetEventRx();
    BT_ResetHCIState();
    BT_ResetVersionState();
    BT_ResetAddressState();
    BT_ResetGenericCommand();

    return USBH_OK;
}

static USBH_StatusTypeDef BT_Request(USBH_HandleTypeDef *phost)
{
    (void)phost;
    return USBH_OK;
}

/* ------------------------------------------------------------- */
/* Generic HCI command engine                                    */
/* ------------------------------------------------------------- */

static USBH_StatusTypeDef BT_HCI_QueueCommand(uint16_t opcode, const uint8_t *params, uint8_t length)
{
    uint8_t i;

    if (BT_Endpoints.PipesReady == 0U) { return USBH_FAIL; }
    if ((length > BT_HCI_MAX_CMD_PARAMS) || ((length != 0U) && (params == NULL))) { return USBH_FAIL; }
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
    BT_HCI_CmdState = BT_HCI_CMD_SEND;

    return USBH_OK;
}

USBH_StatusTypeDef BT_HCI_SendCommand(uint16_t opcode, const uint8_t *params, uint8_t length)
{
    if (BT_InitState != BT_INIT_READY) { return USBH_BUSY; }
    return BT_HCI_QueueCommand(opcode, params, length);
}

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
            status = USBH_CtlReq(phost, BT_HCI_CmdBuffer, BT_HCI_CmdLength);

            if (status == USBH_OK)
            {
                if (BT_HCI_CmdOpcode == 0x0C03U) { BT_HCI_ResetUSBComplete = 1U; BT_HCI_ResetState = BT_HCI_RESET_EVENT_WAIT; }
                if (BT_HCI_CmdOpcode == 0x1001U) { BT_VersionUSBComplete = 1U; BT_VersionState = BT_VERSION_EVENT_WAIT; }
                if (BT_HCI_CmdOpcode == 0x1009U) { BT_AddrUSBComplete = 1U; BT_AddrState = BT_ADDR_EVENT_WAIT; }

                BT_HCI_CmdStartTick = phost->Timer;
                BT_HCI_CmdState = BT_HCI_CMD_EVENT_WAIT;
            }
            else if (status != USBH_BUSY)
            {
                BT_HCI_CmdState = BT_HCI_CMD_ERROR;
            }
            else if ((phost->Timer - BT_HCI_CmdStartTick) >= 2000U)
            {
                BT_HCI_CmdState = BT_HCI_CMD_ERROR;
            }
            break;

        case BT_HCI_CMD_EVENT_WAIT:
            if (BT_HCI_CmdEventSeen != 0U) { BT_HCI_CmdState = (BT_HCI_CmdStatus == 0U) ? BT_HCI_CMD_OK : BT_HCI_CMD_ERROR; }
            else if ((phost->Timer - BT_HCI_CmdStartTick) >= 2000U) { BT_HCI_CmdState = BT_HCI_CMD_ERROR; }
            break;

        case BT_HCI_CMD_OK:
            break;

        case BT_HCI_CMD_ERROR:
            break;

        default:
            BT_HCI_CmdState = BT_HCI_CMD_ERROR;
            break;
    }
}

/* ------------------------------------------------------------- */
/* Unified Bluetooth controller initialization                   */
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
/* HCI Command Complete event decoding                           */
/* ------------------------------------------------------------- */

static void BT_DecodeHCIEvent(uint32_t length)
{
    uint32_t j;
    uint16_t opcode;

    if ((length < 6U) || (length > BT_HCI_MAX_RESPONSE)) { return; }
    if ((BT_EventRxBuffer[0] != 0x0EU) || (((uint32_t)BT_EventRxBuffer[1] + 2U) != length)) { return; }
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

    BT_HCI_CmdEventSeen = 1U;
}

/* ------------------------------------------------------------- */
/* USB Host process callback                                     */
/* ------------------------------------------------------------- */

static USBH_StatusTypeDef BT_Process(USBH_HandleTypeDef *phost)
{
    USBH_URBStateTypeDef urb;
    USBH_StatusTypeDef status;
    uint32_t length;
    uint32_t i;
    uint32_t interval;

    if (BT_Endpoints.PipesReady == 0U) { return USBH_OK; }

    interval = (BT_Endpoints.EventInterval == 0U) ? 1U : BT_Endpoints.EventInterval;

    switch (BT_EventRxState)
    {
        case BT_EVENT_RX_SUBMIT:
            if ((BT_Endpoints.EventInMPS == 0U) || (BT_Endpoints.EventInMPS > BT_EVENT_BUFFER_SIZE))
            {
                BT_EventErrorCount++;
                BT_EventRxState = BT_EVENT_RX_ERROR;
                break;
            }

            status = USBH_InterruptReceiveData(phost, BT_EventRxBuffer, (uint8_t)BT_Endpoints.EventInMPS, BT_Endpoints.EventInPipe);

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

                if (length > BT_EVENT_BUFFER_SIZE) { length = BT_EVENT_BUFFER_SIZE; }

                BT_LastEventLength = (uint16_t)length;

                for (i = 0U; i < length; i++) { BT_LastEventData[i] = BT_EventRxBuffer[i]; }

                BT_EventPacketCount++;
                BT_DecodeHCIEvent(length);
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

    BT_ProcessInitialization();
    BT_ProcessHCICommand(phost);

    return USBH_OK;
}

static USBH_StatusTypeDef BT_SOF(USBH_HandleTypeDef *phost)
{
    (void)phost;
    return USBH_OK;
}
