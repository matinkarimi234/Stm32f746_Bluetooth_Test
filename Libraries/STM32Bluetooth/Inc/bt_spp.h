#ifndef BT_SPP_H
#define BT_SPP_H
#include "bt_config.h"
#include <stdint.h>

/* One active ACL connection; SDP local CID 0x0040, RFCOMM local CID 0x0041. */
#define BT_SPP_LOCAL_SDP_CID    0x0040U
#define BT_SPP_LOCAL_RFCOMM_CID 0x0041U
#define BT_SPP_CLOSED           0U
#define BT_SPP_CONFIGURING      1U
#define BT_SPP_OPEN             2U

/* Retained across disconnect; no payloads or security keys in the trace.
 * protocol: 1=L2CAP signal, 2=SDP, 3=RFCOMM; direction: 0=RX, 1=TX.
 * TX entries mean queued, not controller/peer acknowledgement. */
typedef struct {
    uint32_t sequence;
    uint16_t cid, length, status;
    uint8_t direction, protocol, code, identifier;
} BT_SPP_Event;
typedef struct {
    uint32_t sabm_rx, ua_tx, pn_rx, msc_rx, msc_rsp_rx, uih_rx;
    uint32_t sdp_errors, echo_drops, closes;
    uint16_t info_type, info_result, sdp_tid, peer_mtu[2];
    uint16_t serial_pending;
    uint8_t queue_depth, queue_high_water, last_close;
    uint8_t rfcomm_our_config, rfcomm_peer_config;
    uint8_t sdp_request, sdp_response, cfc, tx_credits, rx_credits;
} BT_SPP_Diagnostics;
extern volatile BT_SPP_Diagnostics BT_SPP_Debug;
extern volatile BT_SPP_Event BT_SPP_Trace[32];
extern volatile uint32_t BT_SPP_TraceCount;
extern volatile uint8_t BT_SPP_TraceNext;

extern volatile uint8_t BT_SPP_SDPState;
extern volatile uint8_t BT_SPP_RFCOMMState;
extern volatile uint8_t BT_SPP_SerialOpen;
extern volatile uint16_t BT_SPP_SDPPeerCID;
extern volatile uint16_t BT_SPP_RFCOMMPeerCID;
extern volatile uint8_t BT_SPP_DLCI;
extern volatile uint32_t BT_SPP_SDPPackets;
extern volatile uint32_t BT_SPP_SDPResponses;
extern volatile uint32_t BT_SPP_RFCOMMFrames;
extern volatile uint32_t BT_SPP_RxBytes;
extern volatile uint32_t BT_SPP_TxBytes;
extern volatile uint32_t BT_SPP_Errors;
extern volatile uint32_t BT_SPP_QueueDrops;
extern volatile uint32_t BT_SPP_ConnectedCount;
extern volatile uint8_t BT_SPP_SDP_OurConfigOK;
extern volatile uint8_t BT_SPP_SDP_PeerConfigOK;
extern volatile uint8_t BT_SPP_SDP_ConfigID;
extern volatile uint32_t BT_SPP_ConfigRspCount;
extern volatile uint32_t BT_SPP_ConfigRspAccepted;
extern volatile uint32_t BT_SPP_ConfigRspIgnored;
extern volatile uint8_t BT_SPP_LastConfigRspID;
extern volatile uint16_t BT_SPP_LastConfigRspSCID;
extern volatile uint16_t BT_SPP_LastConfigRspFlags;
extern volatile uint16_t BT_SPP_LastConfigRspResult;
extern volatile uint32_t BT_SPP_RxSignalTraceCount;
extern volatile uint8_t BT_SPP_RxSignalTraceCode[8];
extern volatile uint8_t BT_SPP_RxSignalTraceId[8];
extern volatile uint16_t BT_SPP_RxSignalTraceArg0[8];

typedef void (*BT_SPP_ReceiveCallback)(const uint8_t *data, uint16_t length, void *context);
/* Persists across dongle and ACL resets. Configure before starting USB Host. */
void BT_SPP_SetReceiver(BT_SPP_ReceiveCallback callback, void *context);
void BT_SPP_Reset(void);
void BT_SPP_OnL2CAP(uint16_t cid, const uint8_t *data, uint16_t length);
void BT_SPP_Process(void);
/* Dispatches a transient span to the configured receiver; no default echo. */
void BT_SPP_DataReceived(const uint8_t *data, uint16_t length);
/* Asynchronous transmit; returns 1 if accepted, 0 if not connected or queue full. */
uint8_t BT_SPP_Send(const uint8_t *data, uint16_t length);

#endif
