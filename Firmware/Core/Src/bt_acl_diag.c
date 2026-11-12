
#include "bt_acl_diag.h"
#include "usbh_bluetooth.h"
#include "usbh_ioreq.h"
#include "bt_acl_tx.h"

#define BT_ACL_HCI_PACKET_MAX (BT_ACL_HCI_DATA_MAX + 4U)

/* ------------------------------------------------------------- */
/* Internal buffers and state                                    */
/* ------------------------------------------------------------- */

static uint8_t BT_ACL_USBBuffer[BT_ACL_USB_BUFFER_SIZE];
static uint8_t BT_ACL_HCIBuffer[BT_ACL_HCI_PACKET_MAX];
static uint8_t BT_ACL_L2CAPBuffer[BT_ACL_L2CAP_PDU_MAX];

static uint16_t BT_ACL_DiscardRemaining = 0U;
static uint16_t BT_ACL_L2CAPHandle = 0xFFFFU;
static uint32_t BT_ACL_LastTick = 0U;

/* ------------------------------------------------------------- */
/* Public diagnostic variables                                   */
/* ------------------------------------------------------------- */

volatile uint8_t BT_ACL_RxState = BT_ACL_RX_SUBMIT;

volatile uint32_t BT_ACL_SubmitCount = 0U;
volatile uint32_t BT_ACL_USBPacketCount = 0U;
volatile uint32_t BT_ACL_NAKCount = 0U;
volatile uint32_t BT_ACL_USBErrorCount = 0U;
volatile uint32_t BT_ACL_HCIPacketCount = 0U;
volatile uint32_t BT_ACL_HCIDiscardCount = 0U;
volatile uint32_t BT_ACL_L2CAPCount = 0U;
volatile uint32_t BT_ACL_L2CAPErrorCount = 0U;
volatile uint32_t BT_ACL_SignalCount = 0U;

volatile uint16_t BT_ACL_LastUSBLength = 0U;
volatile uint16_t BT_ACL_LastHandle = 0xFFFFU;
volatile uint8_t BT_ACL_LastPBFlag = 0xFFU;
volatile uint8_t BT_ACL_LastBCFlag = 0xFFU;
volatile uint16_t BT_ACL_LastHCIDataLength = 0U;

volatile uint16_t BT_ACL_LastCID = 0U;
volatile uint16_t BT_ACL_LastL2CAPLength = 0U;
volatile uint8_t BT_ACL_LastSignalCode = 0U;
volatile uint8_t BT_ACL_LastSignalId = 0U;
volatile uint16_t BT_ACL_LastSignalLength = 0U;
volatile uint16_t BT_ACL_LastPSM = 0U;

volatile uint16_t BT_ACL_HCIPartial = 0U;
volatile uint16_t BT_ACL_HCIExpected = 0U;
volatile uint16_t BT_ACL_L2CAPPartial = 0U;
volatile uint16_t BT_ACL_L2CAPExpected = 0U;

/* ------------------------------------------------------------- */
/* Little-endian helper                                          */
/* ------------------------------------------------------------- */

static uint16_t BT_ACL_Read16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* ------------------------------------------------------------- */
/* Reset partial L2CAP packet                                    */
/* ------------------------------------------------------------- */

static void BT_ACL_ClearL2CAP(void)
{
    BT_ACL_L2CAPPartial = 0U;
    BT_ACL_L2CAPExpected = 0U;
    BT_ACL_L2CAPHandle = 0xFFFFU;
}

/* ------------------------------------------------------------- */
/* Step 18B: one BR/EDR SDP L2CAP signaling channel              */
/* ------------------------------------------------------------- */

#define BT_L2CAP_SIG_CID 0x0001U
#define BT_L2CAP_SDP_PSM 0x0001U
#define BT_L2CAP_LOCAL_CID 0x0040U
#define BT_L2CAP_QUEUE_DEPTH 8U
#define BT_L2CAP_QUEUE_CMD_MAX 20U

static uint8_t BT_L2CAP_QueueData[BT_L2CAP_QUEUE_DEPTH][BT_L2CAP_QUEUE_CMD_MAX];
static uint8_t BT_L2CAP_QueueLength[BT_L2CAP_QUEUE_DEPTH];
static uint8_t BT_L2CAP_QueueHead = 0U;
static uint8_t BT_L2CAP_QueueTail = 0U;
static uint8_t BT_L2CAP_QueueCount = 0U;
static uint8_t BT_L2CAP_NextIdentifier = 1U;
static uint8_t BT_L2CAP_ConfigIdentifier = 0U;
static uint8_t BT_L2CAP_PeerConfigOK = 0U;
static uint8_t BT_L2CAP_LocalConfigOK = 0U;
static uint8_t BT_L2CAP_LinkActive = 0U;
static uint16_t BT_L2CAP_LinkHandle = 0xFFFFU;

volatile uint8_t BT_L2CAP_State = BT_L2CAP_CLOSED;
volatile uint16_t BT_L2CAP_RemoteCID = 0U;
volatile uint16_t BT_L2CAP_OurCID = BT_L2CAP_LOCAL_CID;
volatile uint32_t BT_L2CAP_ConnectionRequests = 0U;
volatile uint32_t BT_L2CAP_ConfigRequests = 0U;
volatile uint32_t BT_L2CAP_InfoRequests = 0U;
volatile uint32_t BT_L2CAP_QueuedSignals = 0U;
volatile uint32_t BT_L2CAP_SentSignals = 0U;
volatile uint32_t BT_L2CAP_QueueOverflows = 0U;
volatile uint32_t BT_L2CAP_ProtocolErrors = 0U;
volatile uint32_t BT_L2CAP_SDPDataCount = 0U;
volatile uint8_t BT_L2CAP_LastSDPCode = 0U;

static void BT_L2CAP_Write16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void BT_L2CAP_ResetSession(void)
{
    BT_L2CAP_State = BT_L2CAP_CLOSED;
    BT_L2CAP_RemoteCID = 0U;
    BT_L2CAP_OurCID = BT_L2CAP_LOCAL_CID;
    BT_L2CAP_QueueHead = 0U;
    BT_L2CAP_QueueTail = 0U;
    BT_L2CAP_QueueCount = 0U;
    BT_L2CAP_NextIdentifier = 1U;
    BT_L2CAP_ConfigIdentifier = 0U;
    BT_L2CAP_PeerConfigOK = 0U;
    BT_L2CAP_LocalConfigOK = 0U;
}

static uint8_t BT_L2CAP_QueueSignal(uint8_t code, uint8_t id, const uint8_t *data, uint8_t length)
{
    uint8_t i;
    uint8_t *out;

    if (((uint16_t)length + 4U > BT_L2CAP_QUEUE_CMD_MAX) || (BT_L2CAP_QueueCount >= BT_L2CAP_QUEUE_DEPTH))
    {
        BT_L2CAP_QueueOverflows++;
        return 0U;
    }

    out = BT_L2CAP_QueueData[BT_L2CAP_QueueTail];
    out[0] = code;
    out[1] = id;
    BT_L2CAP_Write16(&out[2], length);
    for (i = 0U; i < length; i++) { out[4U + i] = data[i]; }

    BT_L2CAP_QueueLength[BT_L2CAP_QueueTail] = (uint8_t)(4U + length);
    BT_L2CAP_QueueTail = (uint8_t)((BT_L2CAP_QueueTail + 1U) % BT_L2CAP_QUEUE_DEPTH);
    BT_L2CAP_QueueCount++;
    BT_L2CAP_QueuedSignals++;
    return 1U;
}

static uint8_t BT_L2CAP_GetIdentifier(void)
{
    uint8_t result = BT_L2CAP_NextIdentifier++;
    if (BT_L2CAP_NextIdentifier == 0U) { BT_L2CAP_NextIdentifier = 1U; }
    return result;
}

static void BT_L2CAP_ReplyReject(uint8_t id, uint16_t reason)
{
    uint8_t response[2];
    BT_L2CAP_Write16(response, reason);
    (void)BT_L2CAP_QueueSignal(0x01U, id, response, sizeof(response));
}

static void BT_L2CAP_ReplyRejectCID(uint8_t id, uint16_t destination_cid, uint16_t source_cid)
{
    uint8_t response[6];
    BT_L2CAP_Write16(&response[0], 0x0002U);
    BT_L2CAP_Write16(&response[2], destination_cid);
    BT_L2CAP_Write16(&response[4], source_cid);
    (void)BT_L2CAP_QueueSignal(0x01U, id, response, sizeof(response));
}

static void BT_L2CAP_RefreshOpenState(void)
{
    if ((BT_L2CAP_State == BT_L2CAP_CONFIGURING) && (BT_L2CAP_LocalConfigOK != 0U) && (BT_L2CAP_PeerConfigOK != 0U))
    {
        BT_L2CAP_State = BT_L2CAP_OPEN;
    }
}

static void BT_L2CAP_OnSignal(uint8_t code, uint8_t id, const uint8_t *data, uint16_t length)
{
    uint8_t response[12];
    uint16_t psm;
    uint16_t scid;
    uint16_t cid;
    uint16_t flags;
    uint16_t result;
    uint16_t pos;
    uint8_t opt_type;
    uint8_t opt_len;
    uint8_t accepted;

    if (id == 0U) { BT_L2CAP_ProtocolErrors++; return; }

    switch (code)
    {
        case 0x02U: /* L2CAP Connection Request */
            if (length != 4U) { BT_L2CAP_ProtocolErrors++; break; }
            BT_L2CAP_ConnectionRequests++;
            psm = BT_ACL_Read16(data);
            scid = BT_ACL_Read16(data + 2U);
            BT_ACL_LastPSM = psm;
            result = 0x0000U;
            if (scid < 0x0040U) { result = 0x0006U; } /* Invalid Source CID */
            else if (psm != BT_L2CAP_SDP_PSM) { result = 0x0002U; } /* PSM not supported */
            else if (BT_L2CAP_State != BT_L2CAP_CLOSED) { result = 0x0004U; } /* No resources (single channel) */
            BT_L2CAP_Write16(&response[0], (result == 0U) ? BT_L2CAP_LOCAL_CID : 0U);
            BT_L2CAP_Write16(&response[2], scid);
            BT_L2CAP_Write16(&response[4], result);
            BT_L2CAP_Write16(&response[6], 0U);
            if (BT_L2CAP_QueueSignal(0x03U, id, response, 8U) == 0U) { break; }
            if (result != 0U) { break; }
            BT_L2CAP_RemoteCID = scid;
            BT_L2CAP_State = BT_L2CAP_CONFIGURING;
            BT_L2CAP_ConfigIdentifier = BT_L2CAP_GetIdentifier();
            BT_L2CAP_Write16(&response[0], scid); /* Destination CID: peer's CID */
            BT_L2CAP_Write16(&response[2], 0U); /* Flags */
            if (BT_L2CAP_QueueSignal(0x04U, BT_L2CAP_ConfigIdentifier, response, 4U) == 0U) { BT_L2CAP_ProtocolErrors++; }
            break;

        case 0x04U: /* L2CAP Configuration Request */
            if (length < 4U) { BT_L2CAP_ProtocolErrors++; break; }
            BT_L2CAP_ConfigRequests++;
            cid = BT_ACL_Read16(data);
            flags = BT_ACL_Read16(data + 2U);
            if ((cid != BT_L2CAP_LOCAL_CID) || (BT_L2CAP_State == BT_L2CAP_CLOSED)) { BT_L2CAP_ReplyRejectCID(id, cid, BT_L2CAP_RemoteCID); break; }
            accepted = (uint8_t)((flags == 0U) ? 1U : 0U);
            pos = 4U;
            while ((accepted != 0U) && (pos < length))
            {
                if ((uint32_t)pos + 2U > length) { accepted = 0U; break; }
                opt_type = data[pos];
                opt_len = data[pos + 1U];
                pos = (uint16_t)(pos + 2U);
                if ((uint32_t)pos + opt_len > length) { accepted = 0U; break; }
                if ((opt_type & 0x7FU) == 0x01U) /* MTU option */
                {
                    if ((opt_len != 2U) || (BT_ACL_Read16(data + pos) < 48U)) { accepted = 0U; }
                }
                else if ((opt_type & 0x80U) == 0U) { accepted = 0U; } /* Unknown mandatory option */
                pos = (uint16_t)(pos + opt_len);
            }
            BT_L2CAP_Write16(&response[0], BT_L2CAP_LOCAL_CID); /* Source CID in response */
            BT_L2CAP_Write16(&response[2], 0U); /* Flags */
            BT_L2CAP_Write16(&response[4], (accepted != 0U) ? 0x0000U : 0x0002U); /* Success or rejected */
            if (BT_L2CAP_QueueSignal(0x05U, id, response, 6U) != 0U)
            {
                if (accepted != 0U) { BT_L2CAP_PeerConfigOK = 1U; BT_L2CAP_RefreshOpenState(); }
            }
            if (accepted == 0U) { BT_L2CAP_ProtocolErrors++; }
            break;

        case 0x05U: /* Response to our Configuration Request */
            if ((length < 6U) || (BT_L2CAP_State != BT_L2CAP_CONFIGURING)) { break; }
            if (id != BT_L2CAP_ConfigIdentifier) { break; }
            if (BT_ACL_Read16(data) != BT_L2CAP_LOCAL_CID) { BT_L2CAP_ProtocolErrors++; break; }
            if ((BT_ACL_Read16(data + 2U) == 0U) && (BT_ACL_Read16(data + 4U) == 0U))
            {
                BT_L2CAP_LocalConfigOK = 1U;
                BT_L2CAP_RefreshOpenState();
            }
            else { BT_L2CAP_ProtocolErrors++; }
            break;

        case 0x06U: /* Disconnection Request */
            if (length != 4U) { BT_L2CAP_ProtocolErrors++; break; }
            if ((BT_ACL_Read16(data) != BT_L2CAP_LOCAL_CID) || (BT_ACL_Read16(data + 2U) != BT_L2CAP_RemoteCID)) { BT_L2CAP_ReplyRejectCID(id, BT_ACL_Read16(data), BT_ACL_Read16(data + 2U)); break; }
            if (BT_L2CAP_QueueSignal(0x07U, id, data, 4U) != 0U) { BT_L2CAP_State = BT_L2CAP_CLOSED; BT_L2CAP_RemoteCID = 0U; }
            break;

        case 0x08U: /* Echo Request */
            if (length <= (BT_L2CAP_QUEUE_CMD_MAX - 4U)) { (void)BT_L2CAP_QueueSignal(0x09U, id, data, (uint8_t)length); }
            else { BT_L2CAP_ReplyReject(id, 0x0001U); }
            break;

        case 0x0AU: /* Information Request */
            if (length != 2U) { BT_L2CAP_ProtocolErrors++; break; }
            BT_L2CAP_InfoRequests++;
            result = BT_ACL_Read16(data);
            BT_L2CAP_Write16(&response[0], result);
            if (result == 0x0002U) /* Extended features: advertise none */
            {
                BT_L2CAP_Write16(&response[2], 0x0000U);
                response[4] = 0U;
                response[5] = 0U;
                response[6] = 0U;
                response[7] = 0U;
                (void)BT_L2CAP_QueueSignal(0x0BU, id, response, 8U);
            }
            else
            {
                BT_L2CAP_Write16(&response[2], 0x0001U); /* Not supported */
                (void)BT_L2CAP_QueueSignal(0x0BU, id, response, 4U);
            }
            break;

        case 0x01U: /* Command Reject from peer */
            BT_L2CAP_ProtocolErrors++;
            break;

        case 0x03U: /* Connection Response */
        case 0x07U: /* Disconnection Response */
        case 0x09U: /* Echo Response */
        case 0x0BU: /* Information Response */
            break;

        default:
            if ((code & 0x01U) == 0U) { BT_L2CAP_ReplyReject(id, 0x0000U); }
            break;
    }
}

static void BT_L2CAP_PumpTX(void)
{
    USBH_StatusTypeDef result;
    uint8_t head;

    if ((BT_L2CAP_QueueCount == 0U) || (BT_Connected == 0U)) { return; }
    if ((BT_ACL_TX_BufferState != BT_ACL_BUF_READY) || (BT_ACL_TX_State != BT_ACL_TX_IDLE)) { return; }

    head = BT_L2CAP_QueueHead;
    result = BT_ACL_TX_SendL2CAP(BT_L2CAP_SIG_CID, BT_L2CAP_QueueData[head], BT_L2CAP_QueueLength[head]);
    if (result == USBH_OK)
    {
        BT_L2CAP_QueueHead = (uint8_t)((head + 1U) % BT_L2CAP_QUEUE_DEPTH);
        BT_L2CAP_QueueCount--;
        BT_L2CAP_SentSignals++; /* Queued into HCI ACL TX module, not necessarily delivered */
    }
    else if (result == USBH_FAIL) { BT_L2CAP_ProtocolErrors++; }
}

/* ------------------------------------------------------------- */
/* Reset ACL diagnostics and reassembly                          */
/* ------------------------------------------------------------- */

void BT_ACL_Reset(void)
{
    BT_ACL_RxState = BT_ACL_RX_SUBMIT;

    BT_ACL_SubmitCount = 0U;
    BT_ACL_USBPacketCount = 0U;
    BT_ACL_NAKCount = 0U;
    BT_ACL_USBErrorCount = 0U;
    BT_ACL_HCIPacketCount = 0U;
    BT_ACL_HCIDiscardCount = 0U;
    BT_ACL_L2CAPCount = 0U;
    BT_ACL_L2CAPErrorCount = 0U;
    BT_ACL_SignalCount = 0U;

    BT_ACL_LastUSBLength = 0U;
    BT_ACL_LastHandle = 0xFFFFU;
    BT_ACL_LastPBFlag = 0xFFU;
    BT_ACL_LastBCFlag = 0xFFU;
    BT_ACL_LastHCIDataLength = 0U;

    BT_ACL_LastCID = 0U;
    BT_ACL_LastL2CAPLength = 0U;
    BT_ACL_LastSignalCode = 0U;
    BT_ACL_LastSignalId = 0U;
    BT_ACL_LastSignalLength = 0U;
    BT_ACL_LastPSM = 0U;

    BT_ACL_HCIPartial = 0U;
    BT_ACL_HCIExpected = 0U;
    BT_ACL_DiscardRemaining = 0U;
    BT_ACL_LastTick = 0U;

    BT_ACL_ClearL2CAP();
    BT_L2CAP_ConnectionRequests = 0U;
    BT_L2CAP_ConfigRequests = 0U;
    BT_L2CAP_InfoRequests = 0U;
    BT_L2CAP_QueuedSignals = 0U;
    BT_L2CAP_SentSignals = 0U;
    BT_L2CAP_QueueOverflows = 0U;
    BT_L2CAP_ProtocolErrors = 0U;
    BT_L2CAP_SDPDataCount = 0U;
    BT_L2CAP_LastSDPCode = 0U;
    BT_L2CAP_LinkActive = 0U;
    BT_L2CAP_LinkHandle = 0xFFFFU;
    BT_L2CAP_ResetSession();
}

/* ------------------------------------------------------------- */
/* Decode a complete L2CAP PDU                                   */
/* ------------------------------------------------------------- */

static void BT_ACL_OnL2CAP(const uint8_t *pdu, uint16_t length)
{
    uint16_t payload_length;
    uint16_t cid;
    uint16_t position;
    uint16_t signal_length;
    uint8_t code;

    if (length < 4U) { BT_ACL_L2CAPErrorCount++; return; }

    payload_length = BT_ACL_Read16(&pdu[0]);
    cid = BT_ACL_Read16(&pdu[2]);

    if (((uint32_t)payload_length + 4U) != (uint32_t)length) { BT_ACL_L2CAPErrorCount++; return; }

    BT_ACL_L2CAPCount++;
    BT_ACL_LastCID = cid;
    BT_ACL_LastL2CAPLength = payload_length;

    /* SDP data uses our dynamically assigned local CID. A server is not implemented yet. */
    if (cid == BT_L2CAP_LOCAL_CID)
    {
        if (BT_L2CAP_State == BT_L2CAP_OPEN)
        {
            BT_L2CAP_SDPDataCount++;
            if (payload_length != 0U) { BT_L2CAP_LastSDPCode = pdu[4]; }
        }
        return;
    }

    if (cid != BT_L2CAP_SIG_CID) { return; }

    position = 4U;

    while (((uint32_t)position + 4U) <= (uint32_t)length)
    {
        code = pdu[position];
        signal_length = BT_ACL_Read16(&pdu[position + 2U]);

        if (((uint32_t)position + 4U + signal_length) > length) { BT_ACL_L2CAPErrorCount++; return; }

        BT_ACL_SignalCount++;
        BT_ACL_LastSignalCode = code;
        BT_ACL_LastSignalId = pdu[position + 1U];
        BT_ACL_LastSignalLength = signal_length;

        /* L2CAP Connection Request: extract PSM */
        if ((code == 0x02U) && (signal_length >= 4U))
        {
            BT_ACL_LastPSM = BT_ACL_Read16(&pdu[position + 4U]);
        }

        BT_L2CAP_OnSignal(code, pdu[position + 1U], &pdu[position + 4U], signal_length);

        position = (uint16_t)(position + 4U + signal_length);
    }

    if (position != length) { BT_ACL_L2CAPErrorCount++; }
}

/* ------------------------------------------------------------- */
/* Process one complete HCI ACL packet                           */
/* ------------------------------------------------------------- */

static void BT_ACL_OnHCI(const uint8_t *frame, uint16_t length)
{
    uint16_t header;
    uint16_t handle;
    uint16_t payload_length;
    uint16_t pdu_length;
    uint8_t pb;
    const uint8_t *payload;
    uint16_t i;

    if (length < 4U) { BT_ACL_HCIDiscardCount++; return; }

    header = BT_ACL_Read16(&frame[0]);
    payload_length = BT_ACL_Read16(&frame[2]);

    if (((uint32_t)payload_length + 4U) != length) { BT_ACL_HCIDiscardCount++; return; }

    handle = (uint16_t)(header & 0x0FFFU);
    pb = (uint8_t)((header >> 12) & 0x03U);
    payload = &frame[4];

    BT_ACL_HCIPacketCount++;
    BT_ACL_LastHandle = handle;
    BT_ACL_LastPBFlag = pb;
    BT_ACL_LastBCFlag = (uint8_t)((header >> 14) & 0x03U);
    BT_ACL_LastHCIDataLength = payload_length;

    /* Ignore data belonging to other connection handles */
    if ((BT_Connected == 0U) || (handle != BT_ConnectionHandle)) { return; }

    /* First L2CAP fragment */
    if ((pb == 0U) || (pb == 2U))
    {
        BT_ACL_ClearL2CAP();

        if (payload_length < 4U) { BT_ACL_L2CAPErrorCount++; return; }

        pdu_length = BT_ACL_Read16(payload);

        if (((uint32_t)pdu_length + 4U) > BT_ACL_L2CAP_PDU_MAX) { BT_ACL_L2CAPErrorCount++; return; }

        BT_ACL_L2CAPExpected = (uint16_t)(pdu_length + 4U);
        BT_ACL_L2CAPHandle = handle;
    }
    /* Continuation L2CAP fragment */
    else if ((pb != 1U) || (BT_ACL_L2CAPHandle != handle) || (BT_ACL_L2CAPExpected == 0U))
    {
        BT_ACL_L2CAPErrorCount++;
        return;
    }

    /* Append received L2CAP bytes */
    if (((uint32_t)BT_ACL_L2CAPPartial + payload_length) > BT_ACL_L2CAPExpected)
    {
        BT_ACL_L2CAPErrorCount++;
        BT_ACL_ClearL2CAP();
        return;
    }

    for (i = 0U; i < payload_length; i++) { BT_ACL_L2CAPBuffer[BT_ACL_L2CAPPartial + i] = payload[i]; }

    BT_ACL_L2CAPPartial = (uint16_t)(BT_ACL_L2CAPPartial + payload_length);

    /* Complete L2CAP packet */
    if (BT_ACL_L2CAPPartial == BT_ACL_L2CAPExpected)
    {
        BT_ACL_OnL2CAP(BT_ACL_L2CAPBuffer, BT_ACL_L2CAPExpected);
        BT_ACL_ClearL2CAP();
    }
}

/* ------------------------------------------------------------- */
/* Reassemble HCI ACL data from USB Bulk packets                 */
/* ------------------------------------------------------------- */

static void BT_ACL_AppendUSB(const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint16_t data_length;

    for (i = 0U; i < length; i++)
    {
        /* Discard an oversized HCI ACL frame safely */
        if (BT_ACL_DiscardRemaining != 0U)
        {
            BT_ACL_DiscardRemaining--;
            continue;
        }

        if (BT_ACL_HCIPartial >= BT_ACL_HCI_PACKET_MAX)
        {
            BT_ACL_HCIDiscardCount++;
            BT_ACL_HCIPartial = 0U;
            BT_ACL_HCIExpected = 0U;
        }

        BT_ACL_HCIBuffer[BT_ACL_HCIPartial++] = data[i];

        /* The four-byte HCI ACL header is complete */
        if (BT_ACL_HCIPartial == 4U)
        {
            data_length = BT_ACL_Read16(&BT_ACL_HCIBuffer[2]);

            if (data_length > BT_ACL_HCI_DATA_MAX)
            {
                BT_ACL_HCIDiscardCount++;
                BT_ACL_DiscardRemaining = data_length;
                BT_ACL_HCIPartial = 0U;
                BT_ACL_HCIExpected = 0U;
                continue;
            }

            BT_ACL_HCIExpected = (uint16_t)(4U + data_length);
        }

        /* Complete HCI ACL packet */
        if ((BT_ACL_HCIExpected != 0U) && (BT_ACL_HCIPartial == BT_ACL_HCIExpected))
        {
            BT_ACL_OnHCI(BT_ACL_HCIBuffer, BT_ACL_HCIExpected);
            BT_ACL_HCIPartial = 0U;
            BT_ACL_HCIExpected = 0U;
        }
    }
}

/* ------------------------------------------------------------- */
/* Nonblocking USB Bulk IN receiver                              */
/* ------------------------------------------------------------- */

void BT_ACL_Process(USBH_HandleTypeDef *phost)
{
    USBH_StatusTypeDef status;
    USBH_URBStateTypeDef urb;
    uint32_t length;
    uint16_t mps;

    if (phost == NULL) { return; }
    if ((BT_Endpoints.PipesReady == 0U) || (BT_Connected == 0U))
    {
        if (BT_L2CAP_LinkActive != 0U) { BT_L2CAP_ResetSession(); BT_L2CAP_LinkActive = 0U; }
        return;
    }

    if ((BT_L2CAP_LinkActive == 0U) || (BT_L2CAP_LinkHandle != BT_ConnectionHandle))
    {
        BT_L2CAP_ResetSession();
        BT_L2CAP_LinkActive = 1U;
        BT_L2CAP_LinkHandle = BT_ConnectionHandle;
    }

    mps = BT_Endpoints.ACLInMPS;

    if ((mps == 0U) || (mps > BT_ACL_USB_BUFFER_SIZE))
    {
        BT_ACL_USBErrorCount++;
        BT_ACL_RxState = BT_ACL_RX_STOPPED;
        return;
    }

    switch (BT_ACL_RxState)
    {
        case BT_ACL_RX_SUBMIT:

            status = USBH_BulkReceiveData(phost, BT_ACL_USBBuffer, mps, BT_Endpoints.ACLInPipe);

            if (status == USBH_OK)
            {
                BT_ACL_SubmitCount++;
                BT_ACL_RxState = BT_ACL_RX_WAIT;
            }
            else
            {
                BT_ACL_USBErrorCount++;
                BT_ACL_LastTick = phost->Timer;
                BT_ACL_RxState = BT_ACL_RX_DELAY;
            }
            break;

        case BT_ACL_RX_WAIT:

            urb = USBH_LL_GetURBState(phost, BT_Endpoints.ACLInPipe);

            if (urb == USBH_URB_DONE)
            {
                length = USBH_LL_GetLastXferSize(phost, BT_Endpoints.ACLInPipe);

                if (length > mps)
                {
                    BT_ACL_USBErrorCount++;
                    BT_ACL_RxState = BT_ACL_RX_STOPPED;
                    break;
                }

                BT_ACL_USBPacketCount++;
                BT_ACL_LastUSBLength = (uint16_t)length;

                if (length != 0U) { BT_ACL_AppendUSB(BT_ACL_USBBuffer, (uint16_t)length); }

                BT_ACL_RxState = BT_ACL_RX_SUBMIT;
            }
            else if ((urb == USBH_URB_NOTREADY) || (urb == USBH_URB_NYET))
            {
                BT_ACL_NAKCount++;
                BT_ACL_LastTick = phost->Timer;
                BT_ACL_RxState = BT_ACL_RX_DELAY;
            }
            else if ((urb == USBH_URB_ERROR) || (urb == USBH_URB_STALL))
            {
                BT_ACL_USBErrorCount++;
                BT_ACL_RxState = BT_ACL_RX_STOPPED;
            }
            break;

        case BT_ACL_RX_DELAY:

            if ((phost->Timer - BT_ACL_LastTick) >= 1U) { BT_ACL_RxState = BT_ACL_RX_SUBMIT; }
            break;

        case BT_ACL_RX_STOPPED:
        default:
            break;
    }

    BT_L2CAP_PumpTX();
}
