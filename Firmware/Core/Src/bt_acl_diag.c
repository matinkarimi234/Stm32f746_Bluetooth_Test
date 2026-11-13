
#include "bt_acl_diag.h"
#include "usbh_bluetooth.h"
#include "usbh_ioreq.h"
#include "bt_acl_tx.h"
#include "bt_spp.h"

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
void BT_ACL_LinkClosed(void) { BT_ACL_ClearL2CAP(); }

/* ------------------------------------------------------------- */
/* SDP and RFCOMM L2CAP server moved to bt_spp.c.                  */
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
    BT_SPP_Reset();
}

/* ------------------------------------------------------------- */
/* Decode a complete L2CAP PDU                                   */
/* ------------------------------------------------------------- */

static void BT_ACL_OnL2CAP(const uint8_t *pdu, uint16_t length)
{
    uint16_t payload_length, cid, off, sig_len;

    if (length < 4U) { BT_ACL_L2CAPErrorCount++; return; }

    payload_length = BT_ACL_Read16(pdu);
    cid = BT_ACL_Read16(pdu + 2U);

    if ((uint32_t)payload_length + 4U != (uint32_t)length) { BT_ACL_L2CAPErrorCount++; return; }

    BT_ACL_L2CAPCount++;
    BT_ACL_LastCID = cid;
    BT_ACL_LastL2CAPLength = payload_length;

    if (cid == 0x0001U)
    {
        off = 4U;

        while ((uint32_t)off + 4U <= length)
        {
            sig_len = BT_ACL_Read16(pdu + off + 2U);

            if ((uint32_t)off + 4U + sig_len > length) { BT_ACL_L2CAPErrorCount++; break; }

            BT_ACL_SignalCount++;
            BT_ACL_LastSignalCode = pdu[off];
            BT_ACL_LastSignalId = pdu[off + 1U];
            BT_ACL_LastSignalLength = sig_len;

            if ((pdu[off] == 0x02U) && (sig_len >= 4U)) { BT_ACL_LastPSM = BT_ACL_Read16(pdu + off + 4U); }

            off = (uint16_t)(off + 4U + sig_len);
        }
    }

    BT_SPP_OnL2CAP(cid, pdu + 4U, payload_length);
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
    /* Keep the USB transfer alive across ACL disconnects. USB endpoint toggles
     * belong to the dongle, not to an individual Bluetooth connection. Old
     * handles are discarded by BT_ACL_OnHCI; do not overwrite a live receive. */
    if (BT_Endpoints.PipesReady == 0U)
    {
        BT_ACL_RxState = BT_ACL_RX_SUBMIT;
        BT_ACL_HCIPartial = 0U;
        BT_ACL_HCIExpected = 0U;
        BT_ACL_ClearL2CAP();
        return;
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
                /* This HAL reactivates BULK IN in its channel-halted ISR.
                 * NOTREADY is observational: the SAME transfer is still live.
                 * Resubmitting here races URB_DONE and loses received ACL data.
                 * Interrupt IN uses different HAL behavior and must resubmit. */
                BT_ACL_NAKCount++;
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

}
