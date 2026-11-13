
#ifndef BT_ACL_DIAG_H
#define BT_ACL_DIAG_H

#include "usbh_core.h"

#define BT_ACL_USB_BUFFER_SIZE       512U
#define BT_ACL_HCI_DATA_MAX          1024U
#define BT_ACL_L2CAP_PDU_MAX         1024U

#define BT_ACL_RX_SUBMIT             0U
#define BT_ACL_RX_WAIT               1U
#define BT_ACL_RX_DELAY              2U
#define BT_ACL_RX_STOPPED            3U

extern volatile uint8_t BT_ACL_RxState;
extern volatile uint32_t BT_ACL_SubmitCount;
extern volatile uint32_t BT_ACL_USBPacketCount;
extern volatile uint32_t BT_ACL_NAKCount;
extern volatile uint32_t BT_ACL_USBErrorCount;
extern volatile uint32_t BT_ACL_HCIPacketCount;
extern volatile uint32_t BT_ACL_HCIDiscardCount;
extern volatile uint32_t BT_ACL_L2CAPCount;
extern volatile uint32_t BT_ACL_L2CAPErrorCount;
extern volatile uint32_t BT_ACL_SignalCount;

extern volatile uint16_t BT_ACL_LastUSBLength;
extern volatile uint16_t BT_ACL_LastHandle;
extern volatile uint8_t BT_ACL_LastPBFlag;
extern volatile uint8_t BT_ACL_LastBCFlag;
extern volatile uint16_t BT_ACL_LastHCIDataLength;

extern volatile uint16_t BT_ACL_LastCID;
extern volatile uint16_t BT_ACL_LastL2CAPLength;
extern volatile uint8_t BT_ACL_LastSignalCode;
extern volatile uint8_t BT_ACL_LastSignalId;
extern volatile uint16_t BT_ACL_LastSignalLength;
extern volatile uint16_t BT_ACL_LastPSM;

extern volatile uint16_t BT_ACL_HCIPartial;
extern volatile uint16_t BT_ACL_HCIExpected;
extern volatile uint16_t BT_ACL_L2CAPPartial;
extern volatile uint16_t BT_ACL_L2CAPExpected;

/* Step 18B: single SDP L2CAP channel diagnostics */
#define BT_L2CAP_CLOSED              0U
#define BT_L2CAP_CONFIGURING         1U
#define BT_L2CAP_OPEN                2U
extern volatile uint8_t BT_L2CAP_State;
extern volatile uint16_t BT_L2CAP_RemoteCID;
extern volatile uint16_t BT_L2CAP_OurCID;
extern volatile uint32_t BT_L2CAP_ConnectionRequests;
extern volatile uint32_t BT_L2CAP_ConfigRequests;
extern volatile uint32_t BT_L2CAP_InfoRequests;
extern volatile uint32_t BT_L2CAP_QueuedSignals;
extern volatile uint32_t BT_L2CAP_SentSignals;
extern volatile uint32_t BT_L2CAP_QueueOverflows;
extern volatile uint32_t BT_L2CAP_ProtocolErrors;
extern volatile uint32_t BT_L2CAP_SDPDataCount;
extern volatile uint8_t BT_L2CAP_LastSDPCode;

void BT_ACL_Reset(void);
void BT_ACL_LinkClosed(void);
void BT_ACL_Process(USBH_HandleTypeDef *phost);

#endif /* BT_ACL_DIAG_H */
