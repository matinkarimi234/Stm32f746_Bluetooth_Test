
#ifndef BT_HCI_TRACE_H
#define BT_HCI_TRACE_H

#include <stdint.h>

#define BT_TRACE_DEPTH 32U

/* HCI event history */
extern volatile uint8_t BT_TraceCode[BT_TRACE_DEPTH];
extern volatile uint16_t BT_TraceLength[BT_TRACE_DEPTH];
extern volatile uint16_t BT_TraceOpcode[BT_TRACE_DEPTH];
extern volatile uint8_t BT_TraceStatus[BT_TRACE_DEPTH];
extern volatile uint32_t BT_TraceTick[BT_TRACE_DEPTH];

/* Connection and pairing state when each event arrived */
extern volatile uint8_t BT_TraceConnState[BT_TRACE_DEPTH];
extern volatile uint8_t BT_TraceAuthState[BT_TRACE_DEPTH];
extern volatile uint8_t BT_TracePairState[BT_TRACE_DEPTH];

/* Circular buffer information */
extern volatile uint8_t BT_TraceNext;
extern volatile uint32_t BT_TraceCount;

/* Remote IO capability information */
extern volatile uint32_t BT_RemoteIOResponseCount;
extern volatile uint8_t BT_RemoteIOCapability;
extern volatile uint8_t BT_RemoteAuthRequirements;
extern volatile uint8_t BT_RemoteOOBPresent;

/* Pairing event counters */
extern volatile uint32_t BT_UserConfirmRequestCount;
extern volatile uint32_t BT_SimplePairCompleteCount;
extern volatile uint32_t BT_AuthCompleteEventCount;
extern volatile uint32_t BT_LinkKeyNotificationCount;
extern volatile uint8_t BT_LastLinkKeyType;
extern volatile uint8_t BT_TraceCmdCredits[BT_TRACE_DEPTH];

/* Public functions */
void BT_TraceReset(void);

#endif /* BT_HCI_TRACE_H */
