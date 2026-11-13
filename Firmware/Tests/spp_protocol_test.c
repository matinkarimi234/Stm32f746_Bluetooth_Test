/* Native tests compile the production protocol implementation directly. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bt_spp.h"
#include "bt_acl_tx.h"
#include "usbh_bluetooth.h"
volatile uint8_t BT_Connected = 1;
volatile uint16_t BT_ConnectionHandle = 1;
volatile uint8_t BT_ACL_TX_BufferState = BT_ACL_BUF_READY, BT_ACL_TX_State = BT_ACL_TX_IDLE;
static uint8_t sent[384];
static uint16_t sent_len, sent_cid;
static unsigned sent_count;
USBH_StatusTypeDef BT_ACL_TX_SendL2CAP(uint16_t cid, const uint8_t *p, uint16_t n)
{ assert(n <= sizeof(sent)); memcpy(sent,p,n); sent_len=n; sent_cid=cid; sent_count++; return USBH_OK; }
#include "../Core/Src/bt_spp.c"
static void fresh(void) { BT_Connected=0; BT_SPP_Reset(); BT_Connected=1; BT_SPP_Process(); sent_count=0; }
static void sig(uint8_t code, uint8_t id, const uint8_t *p, uint16_t n)
{ uint8_t b[40]; b[0]=code; b[1]=id; b[2]=(uint8_t)n; b[3]=0; memcpy(b+4,p,n); BT_SPP_OnL2CAP(1,b,n+4); }
static void drain(void) { unsigned guard=100; while(qcount && guard--) BT_SPP_Process(); assert(guard); }
static void open_channel(unsigned idx, uint16_t peer)
{
 uint8_t b[8]; uint8_t config_id;
 wr16(b, idx ? 3:1); wr16(b+2,peer); sig(2,0x31,b,4); drain();
 assert(sent_cid==1 && sent[0]==4 && rd16(sent+4)==peer);
 config_id=sent[1];
 /* Response source CID is OUR local CID, never the peer CID. */
 wr16(b,peer); wr16(b+2,0); wr16(b+4,0); sig(5,config_id,b,6);
 assert(channels[idx].our_config_ok==0);
 wr16(b,channels[idx].local_cid); sig(5,config_id,b,6);
 assert(channels[idx].our_config_ok==1 && channels[idx].state==1);
 wr16(b,channels[idx].local_cid); wr16(b+2,0); b[4]=1; b[5]=2; wr16(b+6,48);
 sig(4,0x32,b,8); drain();
 assert(sent[0]==5 && rd16(sent+4)==peer && rd16(sent+8)==0);
 assert(channels[idx].state==2 && channels[idx].peer_mtu==48);
}
static void rf(const uint8_t *b, uint16_t n) { BT_SPP_OnL2CAP(0x41,b,n); }
static void frame(uint8_t dlci,uint8_t ctl,const uint8_t *b,uint8_t n,int credit)
{
 uint8_t f[140]; unsigned pos=3;
 f[0]=(dlci<<2)|3; f[1]=ctl; f[2]=(n<<1)|1;
 if(credit>=0) f[pos++]=(uint8_t)credit;
 if(n) memcpy(f+pos,b,n); pos+=n;
 f[pos++]=rfcomm_fcs(f,(ctl&0xEF)==0xEF?2:3); rf(f,(uint16_t)pos);
}
static void sdp_test(void)
{
 uint8_t req[22]={6,0x12,0x34,0,15,0x35,3,0x19,0x11,1,0,7,0x35,5,0x0A,0,0,0xFF,0xFF,0};
 uint8_t assembled[256]; unsigned total=0, n, i; uint16_t params;
 BT_SPP_OnL2CAP(0x40,req,20); drain();
 assert(sent[0]==7 && be16(sent+1)==0x1234);
 do {
  assert(sent_len<=48 && sent_cid==0x80 && be16(sent+3)==sent_len-5);
  n=be16(sent+5); memcpy(assembled+total,sent+7,n); total+=n;
  if(sent[7+n]==0) break;
  assert(sent[7+n]==2);
  req[19]=2; req[20]=sent[8+n]; req[21]=sent[9+n];
  params=17; req[3]=0; req[4]=(uint8_t)params;
  BT_SPP_OnL2CAP(0x40,req,22); drain();
 } while(total<256);
 assert(total==sdp_elem(assembled,(uint16_t)total));
 /* Every nested service value is a complete data element (record lengths). */
 assert(assembled[0]==0x35 && assembled[2]==0x35 && assembled[3]==total-4);
 for(i=4;i<total;) {
  unsigned size,header; uint8_t d;
  assert(assembled[i]==9); i+=3; d=assembled[i];
  if((d&7)==5) {size=assembled[i+1];header=2;}
  else {size=1U<<(d&7);header=1;}
  i+=header+size; assert(i<=total);
 }
 assert(i==total);
 /* Separate ServiceSearch and ServiceAttribute PDUs; preserve transaction IDs. */
 { uint8_t search[]={2,0,9,0,8,0x35,3,0x19,0x11,1,0,1,0};
   BT_SPP_OnL2CAP(0x40,search,sizeof(search));drain();
   assert(sent[0]==3 && be16(sent+1)==9 && be16(sent+5)==1 && be16(sent+7)==1 && sent_len==14); }
 { uint8_t attr[]={4,0,10,0,12,0,1,0,1,0,64,0x35,3,9,0,4,0};
   BT_SPP_OnL2CAP(0x40,attr,sizeof(attr));drain();
   assert(sent[0]==5 && sent[7]==0x35 && sent[9]==9 && be16(sent+10)==4);
   assert(!memcmp(sent+12,av_0004,sizeof(av_0004)));
   attr[8]=2;BT_SPP_OnL2CAP(0x40,attr,sizeof(attr));drain();assert(sent[0]==1 && be16(sent+5)==2); }
 /* A token from the completed transaction cannot restart it at an arbitrary offset. */
 req[19]=2;req[20]=0;req[21]=1;BT_SPP_OnL2CAP(0x40,req,22);drain();assert(sent[0]==1 && be16(sent+5)==5);
 assert(sizeof(av_0004)==av_0004[1]+2 && av_0004[13]==1);
 /* UUID AND matching and full 128-bit base UUID validation. */
 { uint8_t pattern[]={0x35,6,0x19,0x11,1,0x19,0x11,0x24}; assert(!sdp_pattern_matches(pattern,sizeof(pattern))); }
 { uint8_t pattern[]={0x35,17,0x1C,0,0,0x11,1,0,0,0x10,0,0x80,0,0,0x80,0x5F,0x9B,0x34,0xFB};
   assert(sdp_pattern_matches(pattern,sizeof(pattern))); pattern[18]=0; assert(!sdp_pattern_matches(pattern,sizeof(pattern))); }
}
int main(void)
{
 uint8_t b[12], payload[]={0,1,0x80,0xFF,'H','i'}; unsigned count; uint32_t err;
 fresh(); wr16(b,2); sig(10,7,b,2); drain();
 { uint8_t expected[]={11,7,8,0,2,0,0,0,0,0,0,0}; assert(sent_len==12 && !memcmp(sent,expected,12)); }
 wr16(b,3); sig(10,8,b,2); drain(); assert(sent_len==8 && rd16(sent+6)==1);
 open_channel(0,0x80); sdp_test(); open_channel(1,0x81);
 /* Known RFCOMM FCS vectors, including responder UA C/R. */
 { uint8_t sabm[]={3,0x3F,1,0x1C}; assert(rfcomm_fcs(sabm,3)==0x1C); rf(sabm,4); drain();
   assert(sent_len==4 && sent[0]==3 && sent[1]==0x73 && sent[3]==0xD7); }
 b[0]=0x83;b[1]=17;b[2]=2;b[3]=0xF0;b[4]=0;b[5]=0;wr16(b+6,120);b[8]=0;b[9]=0;
 frame(0,0xEF,b,10,-1); drain(); assert(cfc_enabled && outgoing_credits==0 && rfcomm_mtu==42);
 frame(2,0x3F,NULL,0,-1); drain(); assert(BT_SPP_SerialOpen);
 b[0]=0xE3;b[1]=5;b[2]=11;b[3]=0x8D; frame(0,0xEF,b,4,-1); drain(); assert(sent[3]==0xE1);
 /* Data must remain queued until credit-only UIH arrives. */
 count=sent_count; frame(2,0xEF,payload,sizeof(payload),-1); BT_SPP_Process();
 assert(serial_count==sizeof(payload) && sent_count==count);
 frame(2,0xFF,NULL,0,1); BT_SPP_Process(); assert(serial_count==0 && sent_cid==0x81);
 assert(sent[0]==9 && sent[1]==0xEF && sent[2]==13 && !memcmp(sent+3,payload,sizeof(payload)));
 err=BT_SPP_Errors; {uint8_t bad[]={3,0x3F,1,0}; rf(bad,4);} assert(BT_SPP_Errors==err+1);
 frame(0,0x53,NULL,0,-1); drain(); assert(!BT_SPP_SerialOpen);
 wr16(b,0x41);wr16(b+2,0x81);sig(6,55,b,4);drain();assert(BT_SPP_RFCOMMState==0);
 BT_Connected=0;BT_SPP_Process();assert(!qcount && !serial_count); BT_Connected=1;BT_SPP_Process();open_channel(1,0x90);
 puts("SPP: information, asymmetric CIDs, SDP continuation/record/UUIDs, RFCOMM FCS/PN/MSC, credit-starved echo, teardown/reconnect passed");
 return 0;
}
