#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usbh_bluetooth.h"
volatile BT_EndpointInfo_t BT_Endpoints;
volatile uint8_t BT_InitState=BT_INIT_READY, BT_HCI_CmdState, BT_HCI_CmdEventSeen, BT_HCI_CmdStatus;
volatile uint16_t BT_HCI_CmdOpcode;
volatile uint8_t BT_HCI_ResponseLength, BT_HCI_Response[64], BT_HCI_CmdReplyEvent;
static uint8_t BT_HCI_CmdBuffer[3+BT_HCI_MAX_CMD_PARAMS], BT_HCI_CmdLength, BT_HCI_ResultHeld;
static uint32_t BT_HCI_CmdStartTick;
volatile uint8_t BT_HCI_DebugFailStage, BT_HCI_DebugUSBStatus, BT_HCI_DebugControlState, BT_HCI_DebugUSBCompleted;
volatile uint8_t BT_DebugOutURB, BT_HCI_ResetUSBComplete, BT_HCI_ResetState;
volatile uint8_t BT_VersionUSBComplete, BT_VersionState, BT_AddrUSBComplete, BT_AddrState;
volatile uint32_t BT_DebugOutURBCount[6];
#define BT_PAIR_QUEUE_SIZE 4U
typedef struct { uint16_t opcode; uint8_t length, params[22]; } BT_PairReply;
static BT_PairReply BT_PairQueue[4];
static uint8_t BT_PairQueueHead, BT_PairQueueTail, BT_PairQueueCount;
static uint8_t BT_PairCmdParams[22], BT_PairCmdLength;
static uint16_t BT_PairCmdOpcode;
volatile uint8_t BT_PairCmdState, BT_PairLastCommandStatus;
volatile uint16_t BT_PairLastOpcode;
volatile uint32_t BT_PairDroppedCount, BT_PairErrorCount, BT_PairReplyCount;
static USBH_StatusTypeDef BT_HCI_QueueCommand(uint16_t,const uint8_t *,uint8_t);
USBH_StatusTypeDef BT_HCI_SendCommand(uint16_t o,const uint8_t *p,uint8_t n){return BT_HCI_QueueCommand(o,p,n);}
USBH_StatusTypeDef USBH_CtlReq(USBH_HandleTypeDef *h,uint8_t *p,uint16_t n){(void)h;(void)p;(void)n;return USBH_BUSY;}
USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *h,uint8_t pipe){(void)h;(void)pipe;return USBH_URB_IDLE;}
#include "bin/hci_functions.inc"
int main(void)
{
 USBH_HandleTypeDef host={0}; uint8_t a[6]={1,2,3,4,5,6};
 BT_Endpoints.PipesReady=1;BT_HCI_CmdState=BT_HCI_CMD_EVENT_WAIT;BT_HCI_CmdEventSeen=1;
 BT_HCI_CmdOpcode=0x1005;BT_HCI_ResponseLength=13;BT_HCI_CmdStatus=0;
 BT_ProcessHCICommand(&host);assert(BT_HCI_CmdState==BT_HCI_CMD_OK && BT_HCI_ResultHeld==2);
 assert(BT_HCI_QueueCommand(0x0409,a,6)==USBH_BUSY && BT_HCI_CmdOpcode==0x1005 && BT_HCI_ResponseLength==13);
 assert(BT_QueuePairReply(0x040B,a,NULL,0));assert(BT_QueuePairReply(0x042C,a,NULL,0));
 BT_ProcessPairing();assert(BT_PairCmdState==BT_PAIR_SEND && BT_PairQueueCount==1);
 BT_HCI_ResultHeld--;BT_ProcessPairing();assert(BT_PairCmdState==BT_PAIR_SEND && BT_HCI_CmdOpcode==0x1005);
 BT_HCI_ResultHeld--;BT_ProcessPairing();assert(BT_PairCmdState==BT_PAIR_WAIT && BT_HCI_CmdOpcode==0x040B);
 BT_HCI_CmdState=BT_HCI_CMD_EVENT_WAIT;BT_HCI_CmdEventSeen=1;BT_HCI_CmdStatus=0;BT_HCI_ResponseLength=12;BT_HCI_CmdReplyEvent=BT_HCI_EVT_COMMAND_COMPLETE;
 BT_ProcessHCICommand(&host);BT_ProcessPairing();assert(BT_PairCmdState==BT_PAIR_IDLE && BT_PairReplyCount==1);
 BT_HCI_ResultHeld=0;BT_ProcessPairing();assert(BT_HCI_CmdOpcode==0x042C && BT_PairDroppedCount==0);
 BT_HCI_CmdState=BT_HCI_CMD_EVENT_WAIT;BT_HCI_CmdEventSeen=1;BT_HCI_CmdStatus=5;BT_ProcessHCICommand(&host);
 assert(BT_HCI_CmdState==BT_HCI_CMD_ERROR && BT_HCI_ResultHeld==2);
 puts("HCI: completed-result ownership across scheduler passes, queued pairing replies, controller-error retention passed");return 0;
}
