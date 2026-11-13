#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usbh_bluetooth.h"
#include "bt_acl_tx.h"
#include "bt_spp.h"
#include "usbh_ioreq.h"
volatile uint8_t BT_Connected=1, BT_InitState=BT_INIT_READY, BT_ScanState=BT_SCAN_READY;
volatile uint16_t BT_ConnectionHandle=1, BT_HCI_CmdOpcode;
volatile uint8_t BT_HCI_CmdState, BT_HCI_CmdReplyEvent, BT_HCI_ResponseLength, BT_HCI_Response[64];
volatile BT_EndpointInfo_t BT_Endpoints;
static uint8_t *rx_buffer, captured[64], rebuilt[400];
static uint16_t captured_len, rebuilt_len;
static unsigned submits, deliveries;
static uint16_t delivered_cid, delivered_len;
static uint8_t delivered[384];
static USBH_URBStateTypeDef in_urb=USBH_URB_IDLE,out_urb=USBH_URB_IDLE;
static uint32_t rx_len;
USBH_StatusTypeDef BT_HCI_SendCommand(uint16_t o,const uint8_t *p,uint8_t n){(void)o;(void)p;(void)n;return USBH_BUSY;}
void BT_SPP_Reset(void){}
void BT_SPP_OnL2CAP(uint16_t cid,const uint8_t *p,uint16_t n){ deliveries++;delivered_cid=cid;delivered_len=n;memcpy(delivered,p,n); }
USBH_StatusTypeDef USBH_BulkReceiveData(USBH_HandleTypeDef *h,uint8_t *p,uint16_t n,uint8_t pipe)
{(void)h;(void)n;(void)pipe;rx_buffer=p;submits++;in_urb=USBH_URB_IDLE;return USBH_OK;}
USBH_StatusTypeDef USBH_BulkSendData(USBH_HandleTypeDef *h,uint8_t *p,uint16_t n,uint8_t pipe,uint8_t ping)
{(void)h;(void)pipe;(void)ping;memcpy(captured,p,n);captured_len=n;out_urb=USBH_URB_IDLE;return USBH_OK;}
USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *h,uint8_t pipe){(void)h;return pipe==1?in_urb:out_urb;}
uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *h,uint8_t pipe){(void)h;(void)pipe;return rx_len;}
#include "../Core/Src/bt_acl_diag.c"
#include "../Core/Src/bt_acl_tx.c"
static void input(USBH_HandleTypeDef *h,const uint8_t *b,unsigned n)
{BT_ACL_Process(h);assert(BT_ACL_RxState==BT_ACL_RX_WAIT);memcpy(rx_buffer,b,n);rx_len=n;in_urb=USBH_URB_DONE;BT_ACL_Process(h);}
int main(void)
{
 USBH_HandleTypeDef host={0}; uint8_t info[]={1,0x20,10,0,6,0,1,0,10,3,2,0,2,0};
 uint8_t payload[200],event[]={0x13,5,1,1,0,1,0}; unsigned i,fragments=0;
 BT_Endpoints.PipesReady=1; BT_Endpoints.ACLInPipe=1;BT_Endpoints.ACLOutPipe=2;BT_Endpoints.ACLInMPS=BT_Endpoints.ACLOutMPS=64;
 BT_ACL_Reset(); BT_ACL_Process(&host); assert(submits==1);
 in_urb=USBH_URB_NOTREADY;BT_ACL_Process(&host);host.Timer+=2;
 /* ISR completes after NOTREADY was sampled; app must consume DONE, not submit. */
 memcpy(rx_buffer,info,sizeof(info));rx_len=sizeof(info);in_urb=USBH_URB_DONE;
 BT_ACL_Process(&host);BT_ACL_Process(&host);
 assert(deliveries==1 && submits==2 && delivered_cid==1 && delivered_len==6);
 /* Split one HCI frame across USB chunks. */
 input(&host,info,3);input(&host,info+3,sizeof(info)-3);assert(deliveries==2);
 /* Reassemble an L2CAP PDU split across two HCI ACL packets. */
 {uint8_t first[]={1,0x20,6,0,6,0,0x41,0,1,2},last[]={1,0x10,4,0,3,4,5,6};
 input(&host,first,sizeof(first));input(&host,last,sizeof(last));assert(deliveries==3 && delivered_len==6 && delivered[5]==6);}
 for(i=0;i<sizeof(payload);i++)payload[i]=(uint8_t)i;
 BT_ACL_TX_Reset();BT_ACL_TX_BufferState=BT_ACL_BUF_READY;BT_ACL_TX_ControllerMaxData=50;BT_ACL_TX_ControllerTotalPackets=1;
 BT_ACL_TX_Process(&host);assert(BT_ACL_TX_SendL2CAP(0x81,payload,sizeof(payload))==USBH_OK);
 while(BT_ACL_TX_State!=BT_ACL_TX_IDLE){
  BT_ACL_TX_Process(&host);
  if(BT_ACL_TX_State==BT_ACL_TX_WAIT_URB){
   assert(captured_len<=54 && read16(captured+2)==captured_len-4);
   assert((read16(captured)>>12)==(fragments?1:2));
   memcpy(rebuilt+rebuilt_len,captured+4,captured_len-4);rebuilt_len+=captured_len-4;
   if(!fragments){out_urb=USBH_URB_NOTREADY;BT_ACL_TX_Process(&host);host.Timer++;BT_ACL_TX_Process(&host);assert(BT_ACL_TX_Credits==0);}
   out_urb=USBH_URB_DONE;BT_ACL_TX_Process(&host);BT_ACL_TX_OnHCIEvent(event,sizeof(event));fragments++;
  }
  assert(fragments<10);
 }
 assert(fragments==5 && rebuilt_len==204 && read16(rebuilt)==200 && !memcmp(rebuilt+4,payload,200));
 assert(BT_ACL_TX_Credits==1 && BT_ACL_TX_CompletedPacketCount==5);
 BT_Connected=0;BT_ACL_TX_Process(&host);BT_Connected=1;BT_ACL_TX_Process(&host);assert(BT_ACL_TX_SendL2CAP(0x90,payload,10)==USBH_OK);
 /* A disconnect/reconnect with the same controller handle must not overwrite
  * a still-live USB OUT buffer. */
 BT_ACL_TX_Process(&host);assert(BT_ACL_TX_State==BT_ACL_TX_WAIT_URB);
 BT_ACL_TX_LinkClosed();BT_ACL_TX_Process(&host);
 assert(BT_ACL_TX_State==BT_ACL_TX_WAIT_URB && BT_ACL_TX_SendL2CAP(0x90,payload,3)==USBH_BUSY);
 out_urb=USBH_URB_DONE;BT_ACL_TX_Process(&host);BT_ACL_TX_Process(&host);
 assert(BT_ACL_TX_State==BT_ACL_TX_IDLE && BT_ACL_TX_SendL2CAP(0x90,payload,3)==USBH_OK);
 puts("ACL: NAK/DONE race, split USB/HCI/L2CAP RX, PB fragmentation, credits, OUT retry, reconnect passed");return 0;
}
