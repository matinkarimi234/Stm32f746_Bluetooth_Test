#ifndef BT_ACL_TX_H
#define BT_ACL_TX_H
#include "usbh_core.h"

#define BT_ACL_TX_MAX_L2CAP_PAYLOAD 384U
#define BT_ACL_TX_BUFFER_SIZE (BT_ACL_TX_MAX_L2CAP_PAYLOAD + 4U)
#define BT_ACL_TX_TIMEOUT_MS 2000U
#define BT_ACL_TX_HCI_READ_BUFFER_SIZE 0x1005U
#define BT_ACL_TX_EVT_COMPLETED_PACKETS 0x13U
#define BT_ACL_BUF_WAIT_SCAN 0U
#define BT_ACL_BUF_SEND 1U
#define BT_ACL_BUF_WAIT 2U
#define BT_ACL_BUF_READY 3U
#define BT_ACL_BUF_ERROR 4U
#define BT_ACL_TX_IDLE 0U
#define BT_ACL_TX_QUEUED 1U
#define BT_ACL_TX_WAIT_URB 2U
#define BT_ACL_TX_RETRY_DELAY 3U
#define BT_ACL_TX_ERROR 4U

extern volatile uint8_t BT_ACL_TX_BufferState;
extern volatile uint8_t BT_ACL_TX_State;
extern volatile uint16_t BT_ACL_TX_ControllerMaxData;
extern volatile uint16_t BT_ACL_TX_ControllerTotalPackets;
extern volatile uint16_t BT_ACL_TX_Credits;
extern volatile uint16_t BT_ACL_TX_FrameLength;
extern volatile uint16_t BT_ACL_TX_LastHandle;
extern volatile uint32_t BT_ACL_TX_QueuedCount;
extern volatile uint32_t BT_ACL_TX_USBDoneCount;
extern volatile uint32_t BT_ACL_TX_CompletedPacketCount;
extern volatile uint32_t BT_ACL_TX_NAKCount;
extern volatile uint32_t BT_ACL_TX_ErrorCount;
extern volatile uint32_t BT_ACL_TX_RejectCount;
extern volatile uint32_t BT_ACL_TX_FragmentCount;
extern volatile uint8_t BT_ACL_TX_LastURB;
void BT_ACL_TX_Reset(void);
/* Preserve a live USB OUT buffer until its URB terminates, then drop old data. */
void BT_ACL_TX_LinkClosed(void);
void BT_ACL_TX_Process(USBH_HandleTypeDef *phost);
void BT_ACL_TX_OnHCIEvent(const uint8_t *event, uint16_t length);
USBH_StatusTypeDef BT_ACL_TX_SendL2CAP(uint16_t cid, const uint8_t *payload, uint16_t length);
USBH_StatusTypeDef BT_ACL_TX_SendEchoRequest(uint8_t identifier);
#endif
