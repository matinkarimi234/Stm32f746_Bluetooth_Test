#include "bt_acl_tx.h"
#include "usbh_bluetooth.h"
#include "usbh_ioreq.h"
#include <string.h>

static uint8_t pdu[BT_ACL_TX_BUFFER_SIZE];
static uint8_t usb_packet[64];
static uint16_t total_bytes, offset, fragment_bytes;
static uint32_t start_tick, retry_tick;
static uint16_t active_handle = 0xFFFFU;
static uint8_t link_closing;
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
volatile uint32_t BT_ACL_TX_FragmentCount = 0U;
volatile uint8_t BT_ACL_TX_LastURB = 0xFFU;
static uint16_t read16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static void write16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void BT_ACL_TX_Reset(void)
{
    total_bytes = offset = fragment_bytes = 0U;
    start_tick = retry_tick = 0UL;
    active_handle = 0xFFFFU;
    link_closing = 0U;
    BT_ACL_TX_BufferState = BT_ACL_BUF_WAIT_SCAN;
    BT_ACL_TX_State = BT_ACL_TX_IDLE;
    BT_ACL_TX_ControllerMaxData = BT_ACL_TX_ControllerTotalPackets = BT_ACL_TX_Credits = 0U;
    BT_ACL_TX_FrameLength = 0U;
    BT_ACL_TX_LastHandle = 0xFFFFU;
    BT_ACL_TX_QueuedCount = BT_ACL_TX_USBDoneCount = BT_ACL_TX_CompletedPacketCount = 0UL;
    BT_ACL_TX_NAKCount = BT_ACL_TX_ErrorCount = BT_ACL_TX_RejectCount = BT_ACL_TX_FragmentCount = 0UL;
    BT_ACL_TX_LastURB = 0xFFU;
}
void BT_ACL_TX_LinkClosed(void) { link_closing = 1U; }
USBH_StatusTypeDef BT_ACL_TX_SendL2CAP(uint16_t cid, const uint8_t *payload, uint16_t length)
{
    if (link_closing) { return USBH_BUSY; }
    if (BT_ACL_TX_BufferState != BT_ACL_BUF_READY || BT_Connected == 0U || BT_ACL_TX_State != BT_ACL_TX_IDLE) { return USBH_BUSY; }
    if (length > BT_ACL_TX_MAX_L2CAP_PAYLOAD || (length && payload == NULL) || BT_ConnectionHandle == 0xFFFFU)
    {
        BT_ACL_TX_RejectCount++; return USBH_FAIL;
    }
    if (!BT_Endpoints.PipesReady || BT_Endpoints.ACLOutPipe == 0xFFU) { return USBH_BUSY; }
    write16(pdu, length);
    write16(pdu + 2U, cid);
    if (length != 0U) { memcpy(pdu + 4U, payload, length); }
    total_bytes = (uint16_t)(length + 4U);
    offset = 0U;
    BT_ACL_TX_LastHandle = BT_ConnectionHandle;
    BT_ACL_TX_State = BT_ACL_TX_QUEUED;
    BT_ACL_TX_QueuedCount++;
    return USBH_OK;
}
USBH_StatusTypeDef BT_ACL_TX_SendEchoRequest(uint8_t identifier)
{
    uint8_t req[4] = {8U, 0U, 0U, 0U}; req[1] = identifier;
    return BT_ACL_TX_SendL2CAP(0x0001U, req, 4U);
}
void BT_ACL_TX_OnHCIEvent(const uint8_t *event, uint16_t length)
{
    uint16_t i, handle, n;
    uint32_t sum;
    if (event == NULL || length < 3U || event[0] != 0x13U) { return; }
    if ((uint32_t)length != 3UL + (uint32_t)event[2] * 4UL) { BT_ACL_TX_ErrorCount++; return; }
    for (i = 0U; i < event[2]; ++i)
    {
        handle = (uint16_t)(read16(event + 3U + 4U * i) & 0x0FFFU);
        n = read16(event + 5U + 4U * i);
        if (BT_Connected && handle == BT_ConnectionHandle)
        {
            BT_ACL_TX_CompletedPacketCount += n;
            sum = (uint32_t)BT_ACL_TX_Credits + n;
            BT_ACL_TX_Credits = (uint16_t)((sum > BT_ACL_TX_ControllerTotalPackets) ? BT_ACL_TX_ControllerTotalPackets : sum);
        }
    }
}
static void buffer_query(void)
{
    USBH_StatusTypeDef r;
    switch (BT_ACL_TX_BufferState)
    {
        case BT_ACL_BUF_WAIT_SCAN:
            if (BT_InitState == BT_INIT_READY && BT_ScanState == BT_SCAN_READY) { BT_ACL_TX_BufferState = BT_ACL_BUF_SEND; }
            break;
        case BT_ACL_BUF_SEND:
            r = BT_HCI_SendCommand(0x1005U, NULL, 0U);
            if (r == USBH_OK) { BT_ACL_TX_BufferState = BT_ACL_BUF_WAIT; }
            else if (r == USBH_FAIL) { BT_ACL_TX_BufferState = BT_ACL_BUF_ERROR; BT_ACL_TX_ErrorCount++; }
            break;
        case BT_ACL_BUF_WAIT:
            if (BT_HCI_CmdOpcode != 0x1005U) { BT_ACL_TX_BufferState = BT_ACL_BUF_ERROR; BT_ACL_TX_ErrorCount++; break; }
            if (BT_HCI_CmdState == BT_HCI_CMD_ERROR) { BT_ACL_TX_BufferState = BT_ACL_BUF_ERROR; BT_ACL_TX_ErrorCount++; break; }
            if (BT_HCI_CmdState != BT_HCI_CMD_OK) { break; }
            if (BT_HCI_CmdReplyEvent != BT_HCI_EVT_COMMAND_COMPLETE || BT_HCI_ResponseLength != 13U || BT_HCI_Response[3] != 5U || BT_HCI_Response[4] != 0x10U || BT_HCI_Response[5] != 0U)
            {
                BT_ACL_TX_BufferState = BT_ACL_BUF_ERROR; BT_ACL_TX_ErrorCount++; break;
            }
            BT_ACL_TX_ControllerMaxData = read16((const uint8_t *)&BT_HCI_Response[6]);
            BT_ACL_TX_ControllerTotalPackets = read16((const uint8_t *)&BT_HCI_Response[9]);
            if (!BT_ACL_TX_ControllerMaxData || !BT_ACL_TX_ControllerTotalPackets) { BT_ACL_TX_BufferState = BT_ACL_BUF_ERROR; BT_ACL_TX_ErrorCount++; break; }
            BT_ACL_TX_Credits = BT_ACL_TX_ControllerTotalPackets;
            BT_ACL_TX_BufferState = BT_ACL_BUF_READY;
            break;
        default: break;
    }
}
void BT_ACL_TX_Process(USBH_HandleTypeDef *phost)
{
    USBH_URBStateTypeDef urb;
    USBH_StatusTypeDef result;
    uint16_t chunk, n, flags;
    if (phost == NULL || !BT_Endpoints.PipesReady) { return; }
    if (BT_ACL_TX_BufferState != BT_ACL_BUF_READY) { buffer_query(); return; }
    if (link_closing || !BT_Connected)
    {
        if (BT_ACL_TX_State == BT_ACL_TX_WAIT_URB)
        {
            urb = USBH_LL_GetURBState(phost, BT_Endpoints.ACLOutPipe);
            if (urb == USBH_URB_IDLE) { return; }
            /* OUT NAK/NYET is halted by this HAL, unlike BULK IN. */
        }
        BT_ACL_TX_State = BT_ACL_TX_IDLE;
        total_bytes = offset = 0U;
        active_handle = 0xFFFFU;
        link_closing = 0U;
        return;
    }
    if (active_handle != BT_ConnectionHandle)
    {
        active_handle = BT_ConnectionHandle;
        BT_ACL_TX_Credits = BT_ACL_TX_ControllerTotalPackets;
        BT_ACL_TX_State = BT_ACL_TX_IDLE;
        total_bytes = offset = 0U;
    }
    switch (BT_ACL_TX_State)
    {
        case BT_ACL_TX_IDLE: break;
        case BT_ACL_TX_QUEUED:
            if (BT_ACL_TX_Credits == 0U) { break; }
            if (BT_Endpoints.ACLOutMPS <= 4U || BT_ACL_TX_ControllerMaxData == 0U) { BT_ACL_TX_State = BT_ACL_TX_ERROR; BT_ACL_TX_ErrorCount++; break; }
            n = (uint16_t)(BT_Endpoints.ACLOutMPS - 4U);
            if (n > sizeof(usb_packet) - 4U) { n = (uint16_t)(sizeof(usb_packet) - 4U); }
            if (n > BT_ACL_TX_ControllerMaxData) { n = BT_ACL_TX_ControllerMaxData; }
            chunk = (uint16_t)(total_bytes - offset);
            if (chunk > n) { chunk = n; }
            fragment_bytes = chunk;
            flags = (uint16_t)((BT_ConnectionHandle & 0x0FFFU) | ((offset == 0U ? 2U : 1U) << 12));
            write16(usb_packet, flags);
            write16(usb_packet + 2U, chunk);
            memcpy(usb_packet + 4U, pdu + offset, chunk);
            BT_ACL_TX_FrameLength = (uint16_t)(chunk + 4U);
            BT_ACL_TX_Credits--;
            result = USBH_BulkSendData(phost, usb_packet, BT_ACL_TX_FrameLength, BT_Endpoints.ACLOutPipe, 0U);
            if (result == USBH_OK) { start_tick = phost->Timer; BT_ACL_TX_State = BT_ACL_TX_WAIT_URB; BT_ACL_TX_FragmentCount++; }
            else { BT_ACL_TX_Credits++; BT_ACL_TX_State = BT_ACL_TX_ERROR; BT_ACL_TX_ErrorCount++; }
            break;
        case BT_ACL_TX_WAIT_URB:
            urb = USBH_LL_GetURBState(phost, BT_Endpoints.ACLOutPipe);
            BT_ACL_TX_LastURB = (uint8_t)urb;
            if (urb == USBH_URB_DONE)
            {
                BT_ACL_TX_USBDoneCount++;
                offset = (uint16_t)(offset + fragment_bytes);
                if (offset == total_bytes) { BT_ACL_TX_State = BT_ACL_TX_IDLE; BT_ACL_TX_FrameLength = 0U; }
                else { BT_ACL_TX_State = BT_ACL_TX_QUEUED; }
            }
            else if (urb == USBH_URB_NOTREADY || urb == USBH_URB_NYET) { BT_ACL_TX_NAKCount++; retry_tick = phost->Timer; BT_ACL_TX_State = BT_ACL_TX_RETRY_DELAY; }
            else if (urb == USBH_URB_ERROR || urb == USBH_URB_STALL || (uint32_t)(phost->Timer - start_tick) >= BT_ACL_TX_TIMEOUT_MS)
            { BT_ACL_TX_State = BT_ACL_TX_ERROR; BT_ACL_TX_ErrorCount++; }
            break;
        case BT_ACL_TX_RETRY_DELAY:
            if ((uint32_t)(phost->Timer - start_tick) >= BT_ACL_TX_TIMEOUT_MS) { BT_ACL_TX_State = BT_ACL_TX_ERROR; BT_ACL_TX_ErrorCount++; break; }
            if ((uint32_t)(phost->Timer - retry_tick) >= 1U)
            {
                result = USBH_BulkSendData(phost, usb_packet, BT_ACL_TX_FrameLength, BT_Endpoints.ACLOutPipe, 0U);
                if (result == USBH_OK) { BT_ACL_TX_State = BT_ACL_TX_WAIT_URB; }
                else { BT_ACL_TX_State = BT_ACL_TX_ERROR; BT_ACL_TX_ErrorCount++; }
            }
            break;
        default: break;
    }
}
