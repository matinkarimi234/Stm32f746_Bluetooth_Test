
#include "bt_hci_trace.h"
#include "usbh_bluetooth.h"
#include "bt_port.h"

/* ------------------------------------------------------------- */
/* HCI Event History                                             */
/* ------------------------------------------------------------- */

volatile uint8_t BT_TraceCode[BT_TRACE_DEPTH] = {0U};
volatile uint16_t BT_TraceLength[BT_TRACE_DEPTH] = {0U};
volatile uint16_t BT_TraceOpcode[BT_TRACE_DEPTH] = {0U};
volatile uint8_t BT_TraceStatus[BT_TRACE_DEPTH] = {0U};
volatile uint32_t BT_TraceTick[BT_TRACE_DEPTH] = {0U};

/* Connection and pairing snapshots */
volatile uint8_t BT_TraceConnState[BT_TRACE_DEPTH] = {0U};
volatile uint8_t BT_TraceAuthState[BT_TRACE_DEPTH] = {0U};
volatile uint8_t BT_TracePairState[BT_TRACE_DEPTH] = {0U};
volatile uint8_t BT_TraceCmdCredits[BT_TRACE_DEPTH] = {0U};

/* Circular buffer position */
volatile uint8_t BT_TraceNext = 0U;
volatile uint32_t BT_TraceCount = 0U;

/* ------------------------------------------------------------- */
/* Remote IO Capability Information                              */
/* ------------------------------------------------------------- */

volatile uint32_t BT_RemoteIOResponseCount = 0U;
volatile uint8_t BT_RemoteIOCapability = 0xFFU;
volatile uint8_t BT_RemoteAuthRequirements = 0xFFU;
volatile uint8_t BT_RemoteOOBPresent = 0xFFU;

/* ------------------------------------------------------------- */
/* Pairing Event Counters                                        */
/* ------------------------------------------------------------- */

volatile uint32_t BT_UserConfirmRequestCount = 0U;
volatile uint32_t BT_SimplePairCompleteCount = 0U;
volatile uint32_t BT_AuthCompleteEventCount = 0U;
volatile uint32_t BT_LinkKeyNotificationCount = 0U;
volatile uint8_t BT_LastLinkKeyType = 0xFFU;

/* ------------------------------------------------------------- */
/* Reset HCI Trace                                               */
/* ------------------------------------------------------------- */

void BT_TraceReset(void)
{
    uint8_t i;

    BT_TraceNext = 0U;
    BT_TraceCount = 0U;

    BT_RemoteIOResponseCount = 0U;
    BT_RemoteIOCapability = 0xFFU;
    BT_RemoteAuthRequirements = 0xFFU;
    BT_RemoteOOBPresent = 0xFFU;

    BT_UserConfirmRequestCount = 0U;
    BT_SimplePairCompleteCount = 0U;
    BT_AuthCompleteEventCount = 0U;
    BT_LinkKeyNotificationCount = 0U;
    BT_LastLinkKeyType = 0xFFU;

    for (i = 0U; i < BT_TRACE_DEPTH; i++)
    {
        BT_TraceCode[i] = 0U;
        BT_TraceLength[i] = 0U;
        BT_TraceOpcode[i] = 0U;
        BT_TraceStatus[i] = 0xFFU;
        BT_TraceTick[i] = 0U;
        BT_TraceConnState[i] = 0U;
        BT_TraceAuthState[i] = 0U;
        BT_TracePairState[i] = 0U;
				BT_TraceCmdCredits[i] = 0xFFU;
    }
}

/* ------------------------------------------------------------- */
/* HCI Event Callback                                            */
/* ------------------------------------------------------------- */

void BT_HCI_EventCallback(const uint8_t *event, uint16_t length)
{
    uint8_t slot;
    uint8_t code;

    if ((event == 0) || (length < 2U)) { return; }
    if ((uint16_t)(event[1] + 2U) != length) { return; }

    code = event[0];
    slot = BT_TraceNext;

    /* Record basic event information */
    BT_TraceCode[slot] = code;
    BT_TraceLength[slot] = length;
    BT_TraceOpcode[slot] = 0U;
    BT_TraceStatus[slot] = 0xFFU;
		BT_TraceCmdCredits[slot] = 0xFFU;
    BT_TraceTick[slot] = BT_Port_GetTick();

    /* Snapshot Bluetooth state */
    BT_TraceConnState[slot] = BT_ConnState;
    BT_TraceAuthState[slot] = BT_AuthState;
    BT_TracePairState[slot] = BT_PairCmdState;

    /* --------------------------------------------------------- */
    /* HCI Command Complete                                      */
    /* --------------------------------------------------------- */

    if ((code == 0x0EU) && (length >= 6U))
    {
        BT_TraceOpcode[slot] = (uint16_t)event[3] | ((uint16_t)event[4] << 8);
        BT_TraceStatus[slot] = event[5];
				BT_TraceCmdCredits[slot] = event[2];
    }

    /* --------------------------------------------------------- */
    /* HCI Command Status                                        */
    /* --------------------------------------------------------- */

    else if ((code == 0x0FU) && (length == 6U))
    {
        BT_TraceOpcode[slot] = (uint16_t)event[4] | ((uint16_t)event[5] << 8);
        BT_TraceStatus[slot] = event[2];
				BT_TraceCmdCredits[slot] = event[3];
    }

    /* --------------------------------------------------------- */
    /* Events containing controller status                       */
    /* --------------------------------------------------------- */

    else if (((code == 0x03U) || (code == 0x06U) || (code == 0x08U) || (code == 0x36U)) && (length >= 3U))
    {
        BT_TraceStatus[slot] = event[2];
    }

    /* --------------------------------------------------------- */
    /* Disconnection Complete                                    */
    /* --------------------------------------------------------- */

    else if ((code == 0x05U) && (length == 6U))
    {
        BT_TraceStatus[slot] = event[5];
    }

    /* --------------------------------------------------------- */
    /* Remote IO Capability Response                             */
    /* --------------------------------------------------------- */

    if ((code == 0x32U) && (length == 11U))
    {
        BT_RemoteIOResponseCount++;
        BT_RemoteIOCapability = event[8];
        BT_RemoteOOBPresent = event[9];
        BT_RemoteAuthRequirements = event[10];
    }

    /* --------------------------------------------------------- */
    /* User Confirmation Request                                 */
    /* --------------------------------------------------------- */

    else if ((code == 0x33U) && (length == 12U))
    {
        BT_UserConfirmRequestCount++;
    }

    /* --------------------------------------------------------- */
    /* Simple Pairing Complete                                   */
    /* --------------------------------------------------------- */

    else if ((code == 0x36U) && (length == 9U))
    {
        BT_SimplePairCompleteCount++;
    }

    /* --------------------------------------------------------- */
    /* Authentication Complete                                   */
    /* --------------------------------------------------------- */

    else if ((code == 0x06U) && (length == 5U))
    {
        BT_AuthCompleteEventCount++;
    }

    /* --------------------------------------------------------- */
    /* Link Key Notification                                     */
    /* --------------------------------------------------------- */

    else if ((code == 0x18U) && (length == 25U))
    {
        BT_LinkKeyNotificationCount++;
        BT_LastLinkKeyType = event[24];
    }

    /* --------------------------------------------------------- */
    /* Advance circular buffer                                   */
    /* --------------------------------------------------------- */

    BT_TraceCount++;
    BT_TraceNext = (uint8_t)((slot + 1U) % BT_TRACE_DEPTH);
}
