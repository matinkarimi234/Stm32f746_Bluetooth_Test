
#include "bt_acl_tx.h"
#include "usbh_bluetooth.h"
#include "usbh_ioreq.h"

/* ------------------------------------------------------------- */
/* Internal state                                                */
/* ------------------------------------------------------------- */

static uint8_t BT_ACL_TX_Buffer[BT_ACL_TX_BUFFER_SIZE];
static uint32_t BT_ACL_TX_StartTick = 0U;
static uint32_t BT_ACL_TX_RetryTick = 0U;

/* ------------------------------------------------------------- */
/* Debug variables                                               */
/* ------------------------------------------------------------- */

volatile uint8_t BT_ACL_TX_BufferState = BT_ACL_BUF_WAIT_SCAN;
volatile uint8_t BT_ACL_TX_State = BT_ACL_TX_IDLE;

volatile uint16_t BT_ACL_TX_ControllerMaxData = 0U;
volatile uint16_t BT_ACL_TX_ControllerTotalPackets = 0U;
volatile uint16_t BT_ACL_TX_Credits = 0U;

volatile uint16_t BT_ACL_TX_FrameLength = 0U;
volatile uint16_t BT_ACL_TX_LastHandle = 0xFFFFU;

volatile uint32_t BT_ACL_TX_QueuedCount = 0U;
volatile uint32_t BT_ACL_TX_USBDoneCount = 0U;
volatile uint32_t BT_ACL_TX_CompletedPacketCount = 0U;
volatile uint32_t BT_ACL_TX_NAKCount = 0U;
volatile uint32_t BT_ACL_TX_ErrorCount = 0U;
volatile uint32_t BT_ACL_TX_RejectCount = 0U;

volatile uint8_t BT_ACL_TX_LastURB = 0xFFU;

/* ------------------------------------------------------------- */
/* Little-endian helpers                                         */
/* ------------------------------------------------------------- */

static uint16_t BT_ACL_TX_Read16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void BT_ACL_TX_Write16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
}

/* ------------------------------------------------------------- */
/* Reset                                                        */
/* ------------------------------------------------------------- */

void BT_ACL_TX_Reset(void)
{
    uint16_t i;

    BT_ACL_TX_BufferState = BT_ACL_BUF_WAIT_SCAN;
    BT_ACL_TX_State = BT_ACL_TX_IDLE;

    BT_ACL_TX_ControllerMaxData = 0U;
    BT_ACL_TX_ControllerTotalPackets = 0U;
    BT_ACL_TX_Credits = 0U;

    BT_ACL_TX_FrameLength = 0U;
    BT_ACL_TX_LastHandle = 0xFFFFU;

    BT_ACL_TX_QueuedCount = 0U;
    BT_ACL_TX_USBDoneCount = 0U;
    BT_ACL_TX_CompletedPacketCount = 0U;
    BT_ACL_TX_NAKCount = 0U;
    BT_ACL_TX_ErrorCount = 0U;
    BT_ACL_TX_RejectCount = 0U;
    BT_ACL_TX_LastURB = 0xFFU;

    BT_ACL_TX_StartTick = 0U;
    BT_ACL_TX_RetryTick = 0U;

    for (i = 0U; i < BT_ACL_TX_BUFFER_SIZE; i++) { BT_ACL_TX_Buffer[i] = 0U; }
}

/* ------------------------------------------------------------- */
/* Queue one HCI ACL packet containing one L2CAP PDU             */
/* ------------------------------------------------------------- */

USBH_StatusTypeDef BT_ACL_TX_SendL2CAP(uint16_t cid, const uint8_t *payload, uint16_t payload_length)
{
    uint16_t i;
    uint16_t hci_data_length;
    uint16_t handle_and_flags;

    if ((BT_ACL_TX_BufferState != BT_ACL_BUF_READY) || (BT_Connected == 0U)) { return USBH_BUSY; }
    if (BT_ACL_TX_State != BT_ACL_TX_IDLE) { return USBH_BUSY; }
    if ((payload_length != 0U) && (payload == NULL)) { BT_ACL_TX_RejectCount++; return USBH_FAIL; }
    if ((payload_length > BT_ACL_TX_MAX_L2CAP_PAYLOAD) || (BT_ConnectionHandle == 0xFFFFU)) { BT_ACL_TX_RejectCount++; return USBH_FAIL; }
    if ((BT_Endpoints.PipesReady == 0U) || (BT_Endpoints.ACLOutPipe == 0xFFU)) { return USBH_BUSY; }

    hci_data_length = (uint16_t)(4U + payload_length);

    if ((hci_data_length > BT_ACL_TX_ControllerMaxData) || ((hci_data_length + 4U) > BT_Endpoints.ACLOutMPS))
    {
        BT_ACL_TX_RejectCount++;
        return USBH_FAIL;
    }

    /* 12-bit handle, PB = 2 (start of non-flushable L2CAP PDU), BC = 0 */
    handle_and_flags = (uint16_t)((BT_ConnectionHandle & 0x0FFFU) | (2U << 12));

    /* HCI ACL header */
    BT_ACL_TX_Write16(&BT_ACL_TX_Buffer[0], handle_and_flags);
    BT_ACL_TX_Write16(&BT_ACL_TX_Buffer[2], hci_data_length);

    /* L2CAP header */
    BT_ACL_TX_Write16(&BT_ACL_TX_Buffer[4], payload_length);
    BT_ACL_TX_Write16(&BT_ACL_TX_Buffer[6], cid);

    /* L2CAP payload */
    for (i = 0U; i < payload_length; i++) { BT_ACL_TX_Buffer[8U + i] = payload[i]; }

    BT_ACL_TX_FrameLength = (uint16_t)(8U + payload_length);
    BT_ACL_TX_LastHandle = BT_ConnectionHandle;
    BT_ACL_TX_State = BT_ACL_TX_QUEUED;
    BT_ACL_TX_QueuedCount++;

    return USBH_OK;
}

/* ------------------------------------------------------------- */
/* Temporary L2CAP Echo Request for hardware testing            */
/* ------------------------------------------------------------- */

USBH_StatusTypeDef BT_ACL_TX_SendEchoRequest(uint8_t identifier)
{
    uint8_t echo_request[4] = {0x08U, 0U, 0x00U, 0x00U};

    echo_request[1] = identifier;

    return BT_ACL_TX_SendL2CAP(0x0001U, echo_request, sizeof(echo_request));
}

/* ------------------------------------------------------------- */
/* Handle HCI Number Of Completed Packets event                  */
/* ------------------------------------------------------------- */

void BT_ACL_TX_OnHCIEvent(const uint8_t *event, uint16_t length)
{
    uint8_t count;
    uint8_t i;
    uint16_t offset;
    uint16_t handle;
    uint16_t completed;
    uint32_t credits;

    if ((event == NULL) || (length < 3U)) { return; }
    if (event[0] != BT_ACL_TX_EVT_COMPLETED_PACKETS) { return; }

    count = event[2];

    if (length != ((uint16_t)3U + (uint16_t)4U * count))
    {
        BT_ACL_TX_ErrorCount++;
        return;
    }

    for (i = 0U; i < count; i++)
    {
        offset = (uint16_t)(3U + 4U * i);

        handle = (uint16_t)(BT_ACL_TX_Read16(&event[offset]) & 0x0FFFU);
        completed = BT_ACL_TX_Read16(&event[offset + 2U]);

        if ((BT_Connected != 0U) && (handle == BT_ConnectionHandle))
        {
            BT_ACL_TX_CompletedPacketCount += completed;

            credits = (uint32_t)BT_ACL_TX_Credits + completed;
            BT_ACL_TX_Credits = (uint16_t)((credits > BT_ACL_TX_ControllerTotalPackets) ? BT_ACL_TX_ControllerTotalPackets : credits);
        }
    }
}

/* ------------------------------------------------------------- */
/* Query controller transmit buffer size using HCI 0x1005       */
/* ------------------------------------------------------------- */

static void BT_ACL_TX_ProcessBufferQuery(void)
{
    USBH_StatusTypeDef status;
    uint16_t max_data;
    uint16_t total_packets;

    switch (BT_ACL_TX_BufferState)
    {
        case BT_ACL_BUF_WAIT_SCAN:

            if ((BT_InitState == BT_INIT_READY) && (BT_ScanState == BT_SCAN_READY)) { BT_ACL_TX_BufferState = BT_ACL_BUF_SEND; }
            break;

        case BT_ACL_BUF_SEND:

            status = BT_HCI_SendCommand(BT_ACL_TX_HCI_READ_BUFFER_SIZE, NULL, 0U);

            if (status == USBH_OK) { BT_ACL_TX_BufferState = BT_ACL_BUF_WAIT; }
            else if (status == USBH_FAIL) { BT_ACL_TX_BufferState = BT_ACL_BUF_ERROR; BT_ACL_TX_ErrorCount++; }
            break;

        case BT_ACL_BUF_WAIT:

            if (BT_HCI_CmdOpcode != BT_ACL_TX_HCI_READ_BUFFER_SIZE)
            {
                BT_ACL_TX_BufferState = BT_ACL_BUF_ERROR;
                BT_ACL_TX_ErrorCount++;
                break;
            }

            if (BT_HCI_CmdState == BT_HCI_CMD_ERROR)
            {
                BT_ACL_TX_BufferState = BT_ACL_BUF_ERROR;
                BT_ACL_TX_ErrorCount++;
                break;
            }

            if (BT_HCI_CmdState != BT_HCI_CMD_OK) { break; }

            if ((BT_HCI_CmdReplyEvent != BT_HCI_EVT_COMMAND_COMPLETE) || (BT_HCI_ResponseLength != 13U) || (BT_HCI_Response[3] != 0x05U) || (BT_HCI_Response[4] != 0x10U) || (BT_HCI_Response[5] != 0U))
            {
                BT_ACL_TX_BufferState = BT_ACL_BUF_ERROR;
                BT_ACL_TX_ErrorCount++;
                break;
            }

            max_data = BT_ACL_TX_Read16((const uint8_t *)&BT_HCI_Response[6]);
            total_packets = BT_ACL_TX_Read16((const uint8_t *)&BT_HCI_Response[9]);

            if ((max_data == 0U) || (total_packets == 0U))
            {
                BT_ACL_TX_BufferState = BT_ACL_BUF_ERROR;
                BT_ACL_TX_ErrorCount++;
                break;
            }

            BT_ACL_TX_ControllerMaxData = max_data;
            BT_ACL_TX_ControllerTotalPackets = total_packets;
            BT_ACL_TX_Credits = total_packets;
            BT_ACL_TX_BufferState = BT_ACL_BUF_READY;
            break;

        case BT_ACL_BUF_READY:
        case BT_ACL_BUF_ERROR:
        default:
            break;
    }
}

/* ------------------------------------------------------------- */
/* Nonblocking USB Bulk OUT transmission                         */
/* ------------------------------------------------------------- */

void BT_ACL_TX_Process(USBH_HandleTypeDef *phost)
{
    USBH_StatusTypeDef status;
    USBH_URBStateTypeDef urb;

    if (phost == NULL) { return; }
    if (BT_Endpoints.PipesReady == 0U) { return; }

    if (BT_ACL_TX_BufferState != BT_ACL_BUF_READY)
    {
        BT_ACL_TX_ProcessBufferQuery();
        return;
    }

    if (BT_Connected == 0U)
    {
        if (BT_ACL_TX_State != BT_ACL_TX_IDLE)
        {
            BT_ACL_TX_State = BT_ACL_TX_IDLE;
            BT_ACL_TX_FrameLength = 0U;
        }

        return;
    }

    switch (BT_ACL_TX_State)
    {
        case BT_ACL_TX_IDLE:
            break;

        case BT_ACL_TX_QUEUED:

            if (BT_ACL_TX_Credits == 0U) { break; }

            if ((BT_Endpoints.ACLOutMPS == 0U) || (BT_ACL_TX_FrameLength > BT_Endpoints.ACLOutMPS))
            {
                BT_ACL_TX_State = BT_ACL_TX_ERROR;
                BT_ACL_TX_ErrorCount++;
                break;
            }

            /* Reserve one controller ACL packet buffer */
            BT_ACL_TX_Credits--;

            status = USBH_BulkSendData(phost, BT_ACL_TX_Buffer, BT_ACL_TX_FrameLength, BT_Endpoints.ACLOutPipe, 0U);

            if (status == USBH_OK)
            {
                BT_ACL_TX_StartTick = phost->Timer;
                BT_ACL_TX_State = BT_ACL_TX_WAIT_URB;
            }
            else
            {
                BT_ACL_TX_Credits++;
                BT_ACL_TX_State = BT_ACL_TX_ERROR;
                BT_ACL_TX_ErrorCount++;
            }

            break;

        case BT_ACL_TX_WAIT_URB:

            urb = USBH_LL_GetURBState(phost, BT_Endpoints.ACLOutPipe);
            BT_ACL_TX_LastURB = (uint8_t)urb;

            if (urb == USBH_URB_DONE)
            {
                BT_ACL_TX_USBDoneCount++;
                BT_ACL_TX_FrameLength = 0U;
                BT_ACL_TX_State = BT_ACL_TX_IDLE;
            }
            else if ((urb == USBH_URB_NOTREADY) || (urb == USBH_URB_NYET))
            {
                BT_ACL_TX_NAKCount++;
                BT_ACL_TX_RetryTick = phost->Timer;
                BT_ACL_TX_State = BT_ACL_TX_RETRY_DELAY;
            }
            else if ((urb == USBH_URB_ERROR) || (urb == USBH_URB_STALL))
            {
                BT_ACL_TX_ErrorCount++;
                BT_ACL_TX_State = BT_ACL_TX_ERROR;
            }
            else if ((phost->Timer - BT_ACL_TX_StartTick) > BT_ACL_TX_TIMEOUT_MS)
            {
                BT_ACL_TX_ErrorCount++;
                BT_ACL_TX_State = BT_ACL_TX_ERROR;
            }

            break;

        case BT_ACL_TX_RETRY_DELAY:

            if ((phost->Timer - BT_ACL_TX_StartTick) > BT_ACL_TX_TIMEOUT_MS)
            {
                BT_ACL_TX_ErrorCount++;
                BT_ACL_TX_State = BT_ACL_TX_ERROR;
                break;
            }

            if ((phost->Timer - BT_ACL_TX_RetryTick) >= 1U)
            {
                status = USBH_BulkSendData(phost, BT_ACL_TX_Buffer, BT_ACL_TX_FrameLength, BT_Endpoints.ACLOutPipe, 0U);

                if (status == USBH_OK) { BT_ACL_TX_State = BT_ACL_TX_WAIT_URB; }
                else { BT_ACL_TX_ErrorCount++; BT_ACL_TX_State = BT_ACL_TX_ERROR; }
            }

            break;

        case BT_ACL_TX_ERROR:
        default:
            break;
    }
}
