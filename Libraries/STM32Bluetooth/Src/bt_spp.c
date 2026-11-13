#include "bt_spp.h"
#include "bt_acl_tx.h"
#include "usbh_bluetooth.h"
#include <string.h>

#define L2CAP_CID_SIGNAL 0x0001U
#define SDP_PSM 0x0001U
#define RFCOMM_PSM 0x0003U
#define TX_Q_CAPACITY 12U
#define TX_Q_DATA_MAX 384U
#define SDP_HANDLE 0x00010001UL
#define RFCOMM_PAYLOAD_MTU 120U
#define SERIAL_CAPACITY 1024U

typedef struct {
    uint16_t cid;
    uint16_t length;
    uint8_t bytes[TX_Q_DATA_MAX];
} BTTx;
typedef struct {
    uint16_t psm;
    uint16_t local_cid;
    uint16_t remote_cid;
    uint8_t state;
    uint8_t peer_config_ok;
    uint8_t our_config_ok;
    uint8_t config_id;
    uint16_t peer_mtu;
    uint16_t sdp_offset, sdp_hash;
    uint8_t sdp_continuation;
} BTChannel;

static BTChannel channels[2];
static BTTx q[TX_Q_CAPACITY];
static uint8_t qhead, qtail, qcount, next_ident;
static uint8_t link_active, dlci_open, mux_open, rfcomm_dlci;
static uint16_t link_handle;
static uint8_t incoming_credits, outgoing_credits, cfc_enabled;
static uint16_t rfcomm_mtu;
static uint8_t serial_bytes[SERIAL_CAPACITY];
static uint16_t serial_head, serial_tail, serial_count;
static uint8_t peer_flow_off;
static BT_SPP_ReceiveCallback receive_callback;
static void *receive_context;
void BT_SPP_SetReceiver(BT_SPP_ReceiveCallback callback, void *context)
{ receive_callback = callback; receive_context = context; }
typedef char BT_NameLengthMustFit[(sizeof(BT_DEVICE_NAME) > 1U && sizeof(BT_DEVICE_NAME) <= 65U) ? 1 : -1];
volatile BT_SPP_Diagnostics BT_SPP_Debug;
volatile BT_SPP_Event BT_SPP_Trace[32];
volatile uint32_t BT_SPP_TraceCount = 0U;
volatile uint8_t BT_SPP_TraceNext = 0U;
static void trace(uint8_t dir, uint8_t proto, uint8_t code, uint8_t id,
                  uint16_t cid, uint16_t len, uint16_t status)
{
    volatile BT_SPP_Event *e = &BT_SPP_Trace[BT_SPP_TraceNext];
    e->direction = dir; e->protocol = proto; e->code = code; e->identifier = id;
    e->cid = cid; e->length = len; e->status = status;
    e->sequence = ++BT_SPP_TraceCount;
    BT_SPP_TraceNext = (uint8_t)((BT_SPP_TraceNext + 1U) % 32U);
}

volatile uint8_t BT_SPP_SDPState = 0U;
volatile uint8_t BT_SPP_RFCOMMState = 0U;
volatile uint8_t BT_SPP_SerialOpen = 0U;
volatile uint16_t BT_SPP_SDPPeerCID = 0U;
volatile uint16_t BT_SPP_RFCOMMPeerCID = 0U;
volatile uint8_t BT_SPP_DLCI = 0U;
volatile uint32_t BT_SPP_SDPPackets = 0U;
volatile uint32_t BT_SPP_SDPResponses = 0U;
volatile uint32_t BT_SPP_RFCOMMFrames = 0U;
volatile uint32_t BT_SPP_RxBytes = 0U;
volatile uint32_t BT_SPP_TxBytes = 0U;
volatile uint32_t BT_SPP_Errors = 0U;
volatile uint32_t BT_SPP_QueueDrops = 0U;
volatile uint32_t BT_SPP_ConnectedCount = 0U;
volatile uint8_t BT_SPP_SDP_OurConfigOK = 0U;
volatile uint8_t BT_SPP_SDP_PeerConfigOK = 0U;
volatile uint8_t BT_SPP_SDP_ConfigID = 0U;
volatile uint32_t BT_SPP_ConfigRspCount = 0U;
volatile uint32_t BT_SPP_ConfigRspAccepted = 0U;
volatile uint32_t BT_SPP_ConfigRspIgnored = 0U;
volatile uint8_t BT_SPP_LastConfigRspID = 0U;
volatile uint16_t BT_SPP_LastConfigRspSCID = 0U;
volatile uint16_t BT_SPP_LastConfigRspFlags = 0U;
volatile uint16_t BT_SPP_LastConfigRspResult = 0U;
volatile uint32_t BT_SPP_RxSignalTraceCount = 0U;
volatile uint8_t BT_SPP_RxSignalTraceCode[8] = {0U};
volatile uint8_t BT_SPP_RxSignalTraceId[8] = {0U};
volatile uint16_t BT_SPP_RxSignalTraceArg0[8] = {0U};

/* Retain familiar Step 18B Keil Watch variables. */
volatile uint8_t BT_L2CAP_State = 0U;
volatile uint16_t BT_L2CAP_RemoteCID = 0U;
volatile uint16_t BT_L2CAP_OurCID = BT_SPP_LOCAL_SDP_CID;
volatile uint32_t BT_L2CAP_ConnectionRequests = 0U;
volatile uint32_t BT_L2CAP_ConfigRequests = 0U;
volatile uint32_t BT_L2CAP_InfoRequests = 0U;
volatile uint32_t BT_L2CAP_QueuedSignals = 0U;
volatile uint32_t BT_L2CAP_SentSignals = 0U;
volatile uint32_t BT_L2CAP_QueueOverflows = 0U;
volatile uint32_t BT_L2CAP_ProtocolErrors = 0U;
volatile uint32_t BT_L2CAP_SDPDataCount = 0U;
volatile uint8_t BT_L2CAP_LastSDPCode = 0U;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint16_t be16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void putbe16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static uint8_t queue_bytes(uint16_t cid, const uint8_t *data, uint16_t len)
{
    BTTx *item;
    if ((data == NULL) || (len == 0U) || (len > TX_Q_DATA_MAX) || (qcount >= TX_Q_CAPACITY)) { BT_SPP_QueueDrops++; BT_L2CAP_QueueOverflows++; return 0U; }
    item = &q[qtail];
    item->cid = cid;
    item->length = len;
    memcpy(item->bytes, data, len);
    qtail = (uint8_t)((qtail + 1U) % TX_Q_CAPACITY);
    qcount++;
    BT_SPP_Debug.queue_depth = qcount;
    if (qcount > BT_SPP_Debug.queue_high_water) { BT_SPP_Debug.queue_high_water = qcount; }
    return 1U;
}
static uint8_t signal(uint8_t code, uint8_t id, const uint8_t *params, uint16_t length)
{
    uint8_t data[32];
    if (length > 28U) { BT_SPP_Errors++; return 0U; }
    data[0] = code;
    data[1] = id;
    wr16(data + 2, length);
    if (length != 0U) { memcpy(data + 4, params, length); }
    if (queue_bytes(L2CAP_CID_SIGNAL, data, (uint16_t)(length + 4U))) { BT_L2CAP_QueuedSignals++; trace(1U, 1U, code, id, 1U, length, 0U); return 1U; }
    return 0U;
}
static uint8_t nextid(void) { uint8_t n = next_ident++; if (next_ident == 0U) { next_ident = 1U; } return n; }

static void sync_debug(void)
{
    BT_L2CAP_State = channels[0].state;
    BT_L2CAP_RemoteCID = channels[0].remote_cid;
    BT_SPP_SDPState = channels[0].state;
    BT_SPP_RFCOMMState = channels[1].state;
    BT_SPP_SDPPeerCID = channels[0].remote_cid;
    BT_SPP_RFCOMMPeerCID = channels[1].remote_cid;
    BT_SPP_SerialOpen = (uint8_t)((channels[1].state == BT_SPP_OPEN) && mux_open && dlci_open);
    BT_SPP_DLCI = rfcomm_dlci;
    BT_SPP_SDP_OurConfigOK = channels[0].our_config_ok;
    BT_SPP_SDP_PeerConfigOK = channels[0].peer_config_ok;
    BT_SPP_SDP_ConfigID = channels[0].config_id;
    BT_SPP_Debug.rfcomm_our_config = channels[1].our_config_ok;
    BT_SPP_Debug.rfcomm_peer_config = channels[1].peer_config_ok;
    BT_SPP_Debug.peer_mtu[0] = channels[0].peer_mtu;
    BT_SPP_Debug.peer_mtu[1] = channels[1].peer_mtu;
    BT_SPP_Debug.queue_depth = qcount;
    BT_SPP_Debug.serial_pending = serial_count;
    BT_SPP_Debug.cfc = cfc_enabled;
    BT_SPP_Debug.tx_credits = outgoing_credits;
    BT_SPP_Debug.rx_credits = incoming_credits;
}
void BT_SPP_Reset(void)
{
    if (link_active) { BT_SPP_Debug.closes++; BT_SPP_Debug.last_close = 0x05U; }
    memset(channels, 0, sizeof(channels));
    channels[0].psm = SDP_PSM;
    channels[0].local_cid = BT_SPP_LOCAL_SDP_CID;
    channels[1].psm = RFCOMM_PSM;
    channels[1].local_cid = BT_SPP_LOCAL_RFCOMM_CID;
    channels[0].peer_mtu = channels[1].peer_mtu = 672U;
    qhead = qtail = qcount = 0U;
    serial_head = serial_tail = serial_count = 0U;
    peer_flow_off = 0U;
    next_ident = 1U;
    link_active = 0U;
    link_handle = 0xFFFFU;
    mux_open = dlci_open = rfcomm_dlci = 0U;
    incoming_credits = outgoing_credits = 0U;
    cfc_enabled = 0U;
    rfcomm_mtu = RFCOMM_PAYLOAD_MTU;
    sync_debug();
}
static void channel_reset(BTChannel *ch)
{
    ch->state = BT_SPP_CLOSED;
    ch->peer_config_ok = 0U;
    ch->our_config_ok = 0U;
    ch->config_id = 0U;
    ch->peer_mtu = 672U;
    ch->sdp_continuation = 0U;
    BT_SPP_Debug.closes++;
    BT_SPP_Debug.last_close = 0x06U;
    /* Purge queued data for this channel; preserve other-channel signaling. */
    {
        uint8_t n = qcount, i;
        for (i = 0U; i < n; ++i)
        {
            BTTx item = q[qhead];
            qhead = (uint8_t)((qhead + 1U) % TX_Q_CAPACITY); qcount--;
            if (item.cid != ch->remote_cid) { (void)queue_bytes(item.cid, item.bytes, item.length); }
        }
    }
    ch->remote_cid = 0U;
    if (ch->psm == RFCOMM_PSM)
    {
        mux_open = dlci_open = rfcomm_dlci = 0U;
        incoming_credits = outgoing_credits = 0U;
        cfc_enabled = 0U;
        serial_head = serial_tail = serial_count = 0U;
        peer_flow_off = 0U;
        rfcomm_mtu = RFCOMM_PAYLOAD_MTU;
    }
    sync_debug();
}
static void maybe_open(BTChannel *ch)
{
    if ((ch->state == BT_SPP_CONFIGURING) && ch->peer_config_ok && ch->our_config_ok) { ch->state = BT_SPP_OPEN; }
    if (ch->psm == RFCOMM_PSM && ch->state == BT_SPP_OPEN && rfcomm_mtu > ch->peer_mtu - 6U)
    { rfcomm_mtu = (uint16_t)(ch->peer_mtu - 6U); }
    sync_debug();
}
static void signal_reject(uint8_t id, uint16_t reason)
{
    uint8_t b[2];
    wr16(b, reason);
    (void)signal(0x01U, id, b, 2U);
}
static void handle_signal(uint8_t code, uint8_t id, const uint8_t *p, uint16_t len)
{
    BTChannel *ch = NULL;
    uint8_t rsp[12];
    uint16_t psm, scid, result, cid, flags, off;
    uint8_t accepted, t, opt_len;
    /* Keep a short receive history across the timeout for Keil Watch. */
    {
        uint8_t slot = (uint8_t)(BT_SPP_RxSignalTraceCount % 8U);
        BT_SPP_RxSignalTraceCode[slot] = code;
        BT_SPP_RxSignalTraceId[slot] = id;
        BT_SPP_RxSignalTraceArg0[slot] = (len >= 2U) ? rd16(p) : 0xFFFFU;
        BT_SPP_RxSignalTraceCount++;
    }
    trace(0U, 1U, code, id, 1U, len, (len >= 2U) ? rd16(p) : 0U);
    if (id == 0U) { BT_SPP_Errors++; return; }
    switch (code)
    {
        case 0x02U: /* Connection Request */
            if (len != 4U) { BT_SPP_Errors++; break; }
            BT_L2CAP_ConnectionRequests++;
            psm = rd16(p);
            scid = rd16(p + 2);
            if (psm == SDP_PSM) { ch = &channels[0]; }
            else if (psm == RFCOMM_PSM) { ch = &channels[1]; }
            result = 0x0000U;
            if (scid < 0x0040U) { result = 0x0006U; }
            else if (((channels[0].state != BT_SPP_CLOSED) && (channels[0].remote_cid == scid)) ||
                     ((channels[1].state != BT_SPP_CLOSED) && (channels[1].remote_cid == scid))) { result = 0x0007U; }
            else if (ch == NULL) { result = 0x0002U; }
            else if (ch->state != BT_SPP_CLOSED) { result = 0x0004U; }
            wr16(rsp, (result == 0U) ? ch->local_cid : 0U);
            wr16(rsp + 2, scid);
            wr16(rsp + 4, result);
            wr16(rsp + 6, 0U);
            if (!signal(0x03U, id, rsp, 8U)) { break; }
            if (result != 0U) { break; }
            ch->remote_cid = scid;
            ch->state = BT_SPP_CONFIGURING;
            ch->config_id = nextid();
            wr16(rsp, scid); /* peer destination CID in outgoing Config Request */
            wr16(rsp + 2, 0U);
            (void)signal(0x04U, ch->config_id, rsp, 4U);
            sync_debug();
            break;
        case 0x04U: /* Configuration Request */
            BT_L2CAP_ConfigRequests++;
            if (len < 4U) { BT_SPP_Errors++; break; }
            cid = rd16(p);
            ch = (cid == BT_SPP_LOCAL_SDP_CID) ? &channels[0] : (cid == BT_SPP_LOCAL_RFCOMM_CID) ? &channels[1] : NULL;
            if ((ch == NULL) || (ch->state == BT_SPP_CLOSED))
            {
                wr16(rsp, 2U); wr16(rsp + 2, cid); wr16(rsp + 4, 0U);
                (void)signal(0x01U, id, rsp, 6U);
                break;
            }
            flags = rd16(p + 2);
            accepted = (uint8_t)((flags & 0xFFFEU) == 0U);
            off = 4U;
            while ((accepted != 0U) && (off < len))
            {
                if (off + 2U > len) { accepted = 0U; break; }
                t = p[off++]; opt_len = p[off++];
                if ((uint32_t)off + opt_len > len) { accepted = 0U; break; }
                if ((t & 0x7FU) == 1U)
                {
                    if ((opt_len != 2U) || (rd16(p + off) < 48U)) { accepted = 0U; }
                    else { ch->peer_mtu = rd16(p + off); }
                }
                else if ((t & 0x80U) == 0U) { accepted = 0U; }
                off = (uint16_t)(off + opt_len);
            }
            wr16(rsp, ch->remote_cid); /* peer SCID: destination of this Configuration Response */
            wr16(rsp + 2, 0U);
            wr16(rsp + 4, accepted ? 0U : 2U);
            if (signal(0x05U, id, rsp, 6U) && accepted && ((flags & 1U) == 0U)) { ch->peer_config_ok = 1U; maybe_open(ch); }
            if (!accepted) { BT_SPP_Errors++; }
            break;
        case 0x05U: /* Configuration Response */
            BT_SPP_ConfigRspCount++;
            BT_SPP_LastConfigRspID = id;
            BT_SPP_LastConfigRspSCID = (len >= 2U) ? rd16(p) : 0xFFFFU;
            BT_SPP_LastConfigRspFlags = (len >= 4U) ? rd16(p + 2U) : 0xFFFFU;
            BT_SPP_LastConfigRspResult = (len >= 6U) ? rd16(p + 4U) : 0xFFFFU;
            if (len < 6U) { BT_SPP_Errors++; break; }
            cid = rd16(p);
            ch = (cid == BT_SPP_LOCAL_SDP_CID) ? &channels[0] : (cid == BT_SPP_LOCAL_RFCOMM_CID) ? &channels[1] : NULL;
            if ((ch == NULL) || (ch->state != BT_SPP_CONFIGURING) || (ch->config_id != id))
            {
                BT_SPP_ConfigRspIgnored++;
                break;
            }
            if ((rd16(p + 2) == 0U) && (rd16(p + 4) == 0U))
            {
                ch->our_config_ok = 1U;
                BT_SPP_ConfigRspAccepted++;
                maybe_open(ch);
            }
            else { BT_SPP_Errors++; }
            break;
        case 0x06U: /* Disconnection Request */
            if (len != 4U) { BT_SPP_Errors++; break; }
            cid = rd16(p);
            ch = (cid == BT_SPP_LOCAL_SDP_CID) ? &channels[0] : (cid == BT_SPP_LOCAL_RFCOMM_CID) ? &channels[1] : NULL;
            if ((ch == NULL) || (ch->state == BT_SPP_CLOSED) || (rd16(p + 2) != ch->remote_cid)) { signal_reject(id, 0x0002U); break; }
            if (signal(0x07U, id, p, 4U)) { channel_reset(ch); }
            break;
        case 0x08U: /* Echo Request */
            if (len <= 28U) { (void)signal(0x09U, id, p, len); }
            else { signal_reject(id, 0x0000U); }
            break;
        case 0x0AU: /* Information Request */
            BT_L2CAP_InfoRequests++;
            if (len != 2U) { BT_SPP_Errors++; break; }
            psm = rd16(p); wr16(rsp, psm);
            BT_SPP_Debug.info_type = psm;
            BT_SPP_Debug.info_result = (psm == 1U || psm == 2U) ? 0U : 1U;
            if (psm == 1U) { wr16(rsp + 2, 0U); wr16(rsp + 4, 672U); (void)signal(0x0BU, id, rsp, 6U); break; }
            if (psm == 0x0002U) { wr16(rsp + 2, 0U); memset(rsp + 4, 0, 4U); (void)signal(0x0BU, id, rsp, 8U); }
            else { wr16(rsp + 2, 1U); (void)signal(0x0BU, id, rsp, 4U); }
            break;
        case 0x01U: BT_SPP_Errors++; break;
        default: if ((code & 1U) == 0U) { signal_reject(id, 0U); } break;
    }
}

/* ------------------------ SDP SPP service record ------------------------- */
/* SDP attr/value pairs: handle, service class, protocol, browse group,
 * language base, service profile, service name. */
static const uint8_t av_0000[] = {0x0A,0,1,0,1};
static const uint8_t av_0001[] = {0x35,3,0x19,0x11,0x01};
static const uint8_t av_0004[] = {0x35,12,0x35,3,0x19,1,0,0x35,5,0x19,0,3,0x08,BT_SPP_RFCOMM_CHANNEL};
static const uint8_t av_0005[] = {0x35,3,0x19,0x10,0x02};
static const uint8_t av_0006[] = {0x35,9,0x09,0x65,0x6E,0x09,0x00,0x6A,0x09,0x01,0x00};
static const uint8_t av_0009[] = {0x35,8,0x35,6,0x19,0x11,0x01,0x09,0x01,0x02};
static const struct {
    uint8_t type, length;
    char name[sizeof(BT_DEVICE_NAME)];
} av_0100 = {0x25U, sizeof(BT_DEVICE_NAME) - 1U, BT_DEVICE_NAME};
typedef struct { uint16_t id; const uint8_t *value; uint8_t size; } SDPAttr;
static const SDPAttr attrs[] = {
    {0x0000U,av_0000,sizeof(av_0000)},{0x0001U,av_0001,sizeof(av_0001)},
    {0x0004U,av_0004,sizeof(av_0004)},{0x0005U,av_0005,sizeof(av_0005)},
    {0x0006U,av_0006,sizeof(av_0006)},{0x0009U,av_0009,sizeof(av_0009)},
    {0x0100U,(const uint8_t *)&av_0100,sizeof(av_0100) - 1U}
};

/* Support 8-bit and 16-bit length DES; return total element size. */
static uint16_t sdp_elem(const uint8_t *buf, uint16_t avail)
{
    uint32_t n, header;
    if (avail < 2U) { return 0U; }
    if (buf[0] == 0x35U) { n = buf[1]; header = 2U; }
    else if (buf[0] == 0x36U)
    {
        if (avail < 3U) { return 0U; }
        n = be16(buf + 1); header = 3U;
    }
    else { return 0U; }
    return (n + header <= avail) ? (uint16_t)(n + header) : 0U;
}
/* Every UUID in a search pattern must be present in the record. Decode
 * elements, not byte substrings (including the complete Bluetooth base UUID). */
static uint8_t sdp_pattern_matches(const uint8_t *p, uint16_t len)
{
    static const uint8_t base_tail[12] = {0,0,0x10,0,0x80,0,0,0x80,0x5F,0x9B,0x34,0xFB};
    uint16_t off = (p[0] == 0x35U) ? 2U : 3U, uuid;
    uint8_t n = 0U;
    while (off < len)
    {
        if (p[off] == 0x19U && off + 3U <= len) { uuid = be16(p + off + 1U); off += 3U; }
        else if (p[off] == 0x1AU && off + 5U <= len && p[off+1U] == 0U && p[off+2U] == 0U)
        { uuid = be16(p + off + 3U); off += 5U; }
        else if (p[off] == 0x1CU && off + 17U <= len && p[off+1U] == 0U && p[off+2U] == 0U &&
                 memcmp(p + off + 5U, base_tail, 12U) == 0)
        { uuid = be16(p + off + 3U); off += 17U; }
        else { return 0U; }
        if (uuid != 0x1101U && uuid != 0x0100U && uuid != 0x0003U && uuid != 0x1002U) { return 0U; }
        if (++n > 12U) { return 0U; }
    }
    return (uint8_t)(n != 0U);
}
static uint8_t sdp_selected(uint16_t attr, const uint8_t *list, uint16_t len)
{
    uint16_t i, start, end;
    if ((list == NULL) || (len < 2U)) { return 0U; }
    i = (list[0] == 0x35U) ? 2U : 3U;
    while (i < len)
    {
        if ((list[i] == 0x09U) && (i + 3U <= len))
        {
            if (be16(list + i + 1U) == attr) { return 1U; }
            i = (uint16_t)(i + 3U);
        }
        else if ((list[i] == 0x0AU) && (i + 5U <= len))
        {
            start = be16(list + i + 1U); end = be16(list + i + 3U);
            if ((attr >= start) && (attr <= end)) { return 1U; }
            i = (uint16_t)(i + 5U);
        }
        else { break; }
    }
    return 0U;
}
static uint16_t sdp_build_attrs(uint8_t *out, const uint8_t *selection, uint16_t selection_len, uint8_t outer)
{
    uint16_t pos, start, len, i;
    pos = outer ? 4U : 2U; /* reserve outer+inner DES size bytes */
    start = pos;
    for (i = 0U; i < sizeof(attrs)/sizeof(attrs[0]); ++i)
    {
        if (!sdp_selected(attrs[i].id, selection, selection_len)) { continue; }
        if ((uint32_t)pos + 3U + attrs[i].size > 255U) { break; }
        out[pos++] = 0x09U;
        putbe16(out + pos, attrs[i].id); pos = (uint16_t)(pos + 2U);
        memcpy(out + pos, attrs[i].value, attrs[i].size); pos = (uint16_t)(pos + attrs[i].size);
    }
    len = (uint16_t)(pos - start);
    out[outer ? 2U : 0U] = 0x35U;
    out[outer ? 3U : 1U] = (uint8_t)len;
    if (outer) { out[0] = 0x35U; out[1] = (uint8_t)(len + 2U); }
    return pos;
}
static void sdp_error(uint8_t *rsp, uint16_t tid, uint16_t error, BTChannel *ch)
{
    rsp[0] = 0x01U; putbe16(rsp + 1, tid); putbe16(rsp + 3, 2U); putbe16(rsp + 5, error);
    BT_SPP_Debug.sdp_errors++; BT_SPP_Debug.sdp_response = 1U;
    trace(1U, 2U, 1U, 0U, ch->remote_cid, 7U, error);
    (void)queue_bytes(ch->remote_cid, rsp, 7U);
}
static void handle_sdp(const uint8_t *data, uint16_t length, BTChannel *ch)
{
    uint8_t rsp[TX_Q_DATA_MAX];
    uint8_t list[256];
    uint16_t tid, plen, cursor, a_len, max_size, offset, total, send_len, pos, selection_len, request_hash, i;
    const uint8_t *selection;
    uint8_t op, outer, match, contlen;
    if ((length < 5U) || (ch->state != BT_SPP_OPEN)) { return; }
    BT_SPP_SDPPackets++;
    BT_L2CAP_SDPDataCount++;
    BT_L2CAP_LastSDPCode = data[0];
    op = data[0]; tid = be16(data + 1); plen = be16(data + 3);
    if ((uint32_t)plen + 5U != length) { BT_SPP_Errors++; return; }
    BT_SPP_Debug.sdp_request = op; BT_SPP_Debug.sdp_tid = tid;
    trace(0U, 2U, op, 0U, ch->local_cid, length, tid);
    cursor = 5U;
    if (op == 0x02U || op == 0x06U)
    {
        a_len = sdp_elem(data + cursor, (uint16_t)(length - cursor));
        if (a_len == 0U) { sdp_error(rsp, tid, 3U, ch); return; }
        match = sdp_pattern_matches(data + cursor, a_len);
        cursor = (uint16_t)(cursor + a_len);
    }
    else if (op == 0x04U)
    {
        if (length < 11U) { sdp_error(rsp, tid, 3U, ch); return; }
        match = (data[5] == 0U && data[6] == 1U && data[7] == 0U && data[8] == 1U) ? 1U : 0U;
        cursor = 9U;
    }
    else { sdp_error(rsp, tid, 3U, ch); return; }
    if (cursor + 2U > length) { sdp_error(rsp, tid, 3U, ch); return; }
    max_size = be16(data + cursor); cursor = (uint16_t)(cursor + 2U);
    if (max_size == 0U || (op != 0x02U && max_size < 7U)) { sdp_error(rsp, tid, 3U, ch); return; }
    if (op == 0x02U)
    {
        /* ServiceSearchResponse. No continuation needed for a single record. */
        if ((cursor >= length) || (data[cursor] != 0U) || (cursor + 1U != length)) { sdp_error(rsp, tid, 3U, ch); return; }
        rsp[0] = 0x03U; putbe16(rsp + 1, tid);
        putbe16(rsp + 5, match ? 1U : 0U);
        putbe16(rsp + 7, match ? 1U : 0U);
        pos = 9U;
        if (match) { rsp[pos++] = 0U; rsp[pos++] = 1U; rsp[pos++] = 0U; rsp[pos++] = 1U; }
        rsp[pos++] = 0U;
        putbe16(rsp + 3, (uint16_t)(pos - 5U));
        BT_SPP_Debug.sdp_response = rsp[0];
        trace(1U, 2U, rsp[0], 0U, ch->remote_cid, pos, tid);
        if (queue_bytes(ch->remote_cid, rsp, pos)) { BT_SPP_SDPResponses++; }
        return;
    }
    selection = data + cursor;
    selection_len = sdp_elem(selection, (uint16_t)(length - cursor));
    if (selection_len == 0U) { sdp_error(rsp, tid, 3U, ch); return; }
    cursor = (uint16_t)(cursor + selection_len);
    if (cursor >= length) { sdp_error(rsp, tid, 3U, ch); return; }
    request_hash = op;
    for (i = 5U; i < cursor; ++i) { request_hash = (uint16_t)((request_hash * 31U) ^ data[i]); }
    contlen = data[cursor++];
    if ((uint32_t)cursor + contlen != length) { sdp_error(rsp, tid, 3U, ch); return; }
    if (contlen == 0U) { offset = 0U; ch->sdp_continuation = 0U; }
    else if (contlen == 2U && ch->sdp_continuation && ch->sdp_hash == request_hash &&
             be16(data + cursor) == ch->sdp_offset) { offset = ch->sdp_offset; }
    else { sdp_error(rsp, tid, 5U, ch); return; }
    if (op == 0x04U && !match) { sdp_error(rsp, tid, 2U, ch); return; }
    outer = (op == 0x06U) ? 1U : 0U;
    if (match) { total = sdp_build_attrs(list, selection, selection_len, outer); }
    else { list[0] = 0x35U; list[1] = 0U; total = 2U; }
    if (offset > total) { sdp_error(rsp, tid, 5U, ch); return; }
    send_len = (uint16_t)(total - offset);
    if (send_len > max_size) { send_len = max_size; }
    if (send_len > ch->peer_mtu - 10U) { send_len = (uint16_t)(ch->peer_mtu - 10U); }
    if (send_len > 275U) { send_len = 275U; } /* leave room for next continuation state */
    rsp[0] = outer ? 0x07U : 0x05U;
    putbe16(rsp + 1, tid);
    putbe16(rsp + 5, send_len);
    memcpy(rsp + 7, list + offset, send_len);
    pos = (uint16_t)(7U + send_len);
    if ((uint32_t)offset + send_len < total)
    {
        rsp[pos++] = 2U;
        putbe16(rsp + pos, (uint16_t)(offset + send_len)); pos = (uint16_t)(pos + 2U);
    }
    else { rsp[pos++] = 0U; }
    putbe16(rsp + 3, (uint16_t)(pos - 5U));
    BT_SPP_Debug.sdp_response = rsp[0];
    trace(1U, 2U, rsp[0], 0U, ch->remote_cid, pos, tid);
    if (queue_bytes(ch->remote_cid, rsp, pos))
    {
        BT_SPP_SDPResponses++;
        ch->sdp_offset = (uint16_t)(offset + send_len); ch->sdp_hash = request_hash;
        ch->sdp_continuation = (uint8_t)(ch->sdp_offset < total);
    }
}

/* ----------------------------- RFCOMM ------------------------------------ */
static uint8_t rfcomm_fcs(const uint8_t *data, uint8_t len)
{
    uint8_t fcs = 0xFFU, i, j;
    for (i = 0U; i < len; ++i)
    {
        fcs ^= data[i];
        for (j = 0U; j < 8U; ++j) { fcs = (uint8_t)((fcs >> 1) ^ ((fcs & 1U) ? 0xE0U : 0U)); }
    }
    return (uint8_t)(0xFFU - fcs);
}
static uint8_t rfcomm_send_frame(uint8_t dlci, uint8_t control, const uint8_t *data, uint16_t length, int8_t credit)
{
    uint8_t frame[280];
    uint16_t pos, cap;
    uint8_t header_len;
    if (length > 250U) { BT_SPP_QueueDrops++; return 0U; }
    frame[0] = (uint8_t)((dlci << 2) | (((control & 0xEFU) == 0x63U || (control & 0xEFU) == 0x0FU) ? 3U : 1U));
    frame[1] = control;
    if (length < 128U) { frame[2] = (uint8_t)((length << 1) | 1U); header_len = 3U; }
    else { frame[2] = (uint8_t)((length << 1) & 0xFEU); frame[3] = (uint8_t)(length >> 7); header_len = 4U; }
    pos = header_len;
    if (credit >= 0) { frame[pos++] = (uint8_t)credit; }
    if (length != 0U) { memcpy(frame + pos, data, length); pos = (uint16_t)(pos + length); }
    cap = (uint8_t)((control & 0xEFU) == 0xEFU ? 2U : header_len);
    frame[pos++] = rfcomm_fcs(frame, (uint8_t)cap);
    if (channels[1].state != BT_SPP_OPEN) { return 0U; }
    if (!queue_bytes(channels[1].remote_cid, frame, pos)) { return 0U; }
    trace(1U, 3U, control, dlci, channels[1].remote_cid, pos, 0U);
    if ((control & 0xEFU) == 0x63U) { BT_SPP_Debug.ua_tx++; }
    return 1U;
}
static void rfcomm_mcc(uint8_t type, const uint8_t *params, uint8_t len)
{
    uint8_t payload[24];
    if (len > 20U) { BT_SPP_Errors++; return; }
    payload[0] = type;
    payload[1] = (uint8_t)((len << 1) | 1U);
    if (len != 0U) { memcpy(payload + 2, params, len); }
    (void)rfcomm_send_frame(0U, 0xEFU, payload, (uint16_t)(len + 2U), -1);
}
static void rfcomm_handle_mcc(const uint8_t *p, uint16_t len)
{
    uint8_t params[16];
    uint8_t type, sublen, dlci;
    uint16_t mtu;
    if (len < 2U) { BT_SPP_Errors++; return; }
    type = p[0];
    if ((p[1] & 1U) == 0U) { BT_SPP_Errors++; return; }
    sublen = (uint8_t)(p[1] >> 1);
    if ((uint16_t)sublen + 2U > len) { BT_SPP_Errors++; return; }
    p += 2;
    if (type == 0x83U && sublen == 8U) /* PN request */
    {
        BT_SPP_Debug.pn_rx++;
        memcpy(params, p, 8U);
        dlci = params[0];
        if ((dlci >> 1) != BT_SPP_RFCOMM_CHANNEL) { return; }
        rfcomm_dlci = dlci;
        mtu = rd16(params + 4);
        if (mtu == 0U) { mtu = RFCOMM_PAYLOAD_MTU; }
        rfcomm_mtu = (mtu < RFCOMM_PAYLOAD_MTU) ? mtu : RFCOMM_PAYLOAD_MTU;
        if (rfcomm_mtu > channels[1].peer_mtu - 6U) { rfcomm_mtu = (uint16_t)(channels[1].peer_mtu - 6U); }
        wr16(params + 4, rfcomm_mtu);
        cfc_enabled = (uint8_t)(params[1] == 0xF0U || params[1] == 0xE0U);
        outgoing_credits = params[7];
        incoming_credits = cfc_enabled ? 7U : 0U;
        params[1] = cfc_enabled ? 0xE0U : 0U;
        params[7] = cfc_enabled ? 7U : 0U;
        rfcomm_mcc(0x81U, params, 8U);
        sync_debug();
    }
    else if (type == 0xE3U && sublen >= 2U) /* MSC command */ { BT_SPP_Debug.msc_rx++; rfcomm_mcc(0xE1U, p, sublen); }
    else if (type == 0xE1U && sublen >= 2U) { BT_SPP_Debug.msc_rsp_rx++; }
    else if (type == 0xA3U) { peer_flow_off = 0U; rfcomm_mcc(0xA1U, NULL, 0U); } /* Flow on */
    else if (type == 0x63U) { peer_flow_off = 1U; rfcomm_mcc(0x61U, NULL, 0U); } /* Flow off */
    else if (type == 0x23U) { rfcomm_mcc(0x21U, p, sublen); } /* TEST */
    else if (type == 0x93U && sublen == 1U)
    {
        params[0] = p[0]; params[1] = 3U; params[2] = 3U; params[3] = 0U;
        params[4] = 0x11U; params[5] = 0x13U; params[6] = 0x7FU; params[7] = 0x3FU;
        rfcomm_mcc(0x91U, params, 8U);
    }
    else if (type == 0x93U && sublen == 8U) { rfcomm_mcc(0x91U, p, sublen); } /* RPN */
    else if (type == 0x53U) { rfcomm_mcc(0x51U, p, sublen); } /* RLS */
    else if ((type & 2U) != 0U)
    {
        params[0] = type;
        rfcomm_mcc(0x11U, params, 1U); /* Non-Supported Command */
    }
}
static void handle_rfcomm(const uint8_t *p, uint16_t len)
{
    uint8_t control, dlci, head, fcs_size, credit_flag;
    uint16_t plen, pos, needed;
    uint8_t msc[2];
    if (len < 4U || (p[0] & 1U) == 0U) { BT_SPP_Errors++; return; }
    BT_SPP_RFCOMMFrames++;
    dlci = p[0] >> 2;
    control = (uint8_t)(p[1] & 0xEFU);
    if ((p[2] & 1U) != 0U) { plen = p[2] >> 1; head = 3U; }
    else { if (len < 5U) { BT_SPP_Errors++; return; } plen = (uint16_t)((p[2] >> 1) | ((uint16_t)p[3] << 7)); head = 4U; }
    credit_flag = (uint8_t)(cfc_enabled && (control == 0xEFU) && ((p[1] & 0x10U) != 0U) && (dlci != 0U));
    needed = (uint16_t)(head + plen + 1U + credit_flag);
    if (needed != len) { BT_SPP_Errors++; return; }
    fcs_size = (control == 0xEFU) ? 2U : head;
    if (p[len - 1U] != rfcomm_fcs(p, fcs_size)) { BT_SPP_Errors++; return; }
    trace(0U, 3U, p[1], dlci, channels[1].local_cid, len, 0U);
    pos = head;
    if (credit_flag)
    {
        if ((uint16_t)outgoing_credits + p[pos] <= 255U) { outgoing_credits = (uint8_t)(outgoing_credits + p[pos]); }
        else { outgoing_credits = 255U; }
        pos++;
    }
    if (control == 0x2FU) /* SABM */
    {
        BT_SPP_Debug.sabm_rx++;
        if ((dlci == 0U) || (mux_open && ((dlci >> 1) == BT_SPP_RFCOMM_CHANNEL)))
        {
            if (!rfcomm_send_frame(dlci, 0x73U, NULL, 0U, -1)) { return; }
            if (dlci == 0U) { mux_open = 1U; }
            else
            {
                if (!dlci_open) { BT_SPP_ConnectedCount++; }
                rfcomm_dlci = dlci; dlci_open = 1U;
                msc[0] = (uint8_t)((dlci << 2) | 3U); msc[1] = 0x8DU;
                rfcomm_mcc(0xE3U, msc, 2U);
            }
            sync_debug();
        }
        else { (void)rfcomm_send_frame(dlci, 0x1FU, NULL, 0U, -1); }
    }
    else if (control == 0x43U) /* DISC */
    {
        if (dlci != 0U && dlci != rfcomm_dlci) { return; }
        /* Pending serial data belongs to the DLC being closed. */
        {
            uint8_t n = qcount, i;
            for (i = 0U; i < n; ++i)
            {
                BTTx item = q[qhead];
                qhead = (uint8_t)((qhead + 1U) % TX_Q_CAPACITY); qcount--;
                if (item.cid != channels[1].remote_cid) { (void)queue_bytes(item.cid, item.bytes, item.length); }
            }
        }
        serial_head = serial_tail = serial_count = 0U;
        BT_SPP_Debug.closes++; BT_SPP_Debug.last_close = 0x43U;
        (void)rfcomm_send_frame(dlci, 0x73U, NULL, 0U, -1);
        if (dlci == 0U) { mux_open = 0U; dlci_open = 0U; }
        else { dlci_open = 0U; }
        cfc_enabled = incoming_credits = outgoing_credits = rfcomm_dlci = 0U;
        peer_flow_off = 0U;
        sync_debug();
    }
    else if ((control == 0xEFU) && (dlci == 0U)) { rfcomm_handle_mcc(p + pos, plen); }
    else if ((control == 0xEFU) && dlci_open && (dlci == rfcomm_dlci))
    {
        BT_SPP_Debug.uih_rx++;
        if (plen > rfcomm_mtu) { BT_SPP_Errors++; return; }
        if (cfc_enabled && plen && incoming_credits) { incoming_credits--; }
        if (plen != 0U)
        {
            BT_SPP_RxBytes += plen;
            BT_SPP_DataReceived(p + pos, plen);
        }

    }
}

/* Registered application receiver owns framing and optional echo policy. */
void BT_SPP_DataReceived(const uint8_t *data, uint16_t length)
{
    if (receive_callback != NULL) { receive_callback(data, length, receive_context); }
}
uint8_t BT_SPP_Send(const uint8_t *data, uint16_t length)
{
    uint16_t i;
    if (!BT_SPP_SerialOpen || data == NULL || length > SERIAL_CAPACITY - serial_count) { return 0U; }
    for (i = 0U; i < length; ++i)
    {
        serial_bytes[serial_tail] = data[i];
        serial_tail = (uint16_t)((serial_tail + 1U) % SERIAL_CAPACITY);
    }
    serial_count = (uint16_t)(serial_count + length);
    sync_debug();
    return 1U;
}
static void serial_process(void)
{
    uint8_t bytes[RFCOMM_PAYLOAD_MTU];
    uint16_t n, i;
    /* Reserve four queue entries for control; retain bytes while credits are zero. */
    if (!BT_SPP_SerialOpen || peer_flow_off || serial_count == 0U || qcount >= TX_Q_CAPACITY - 4U ||
        (cfc_enabled && outgoing_credits == 0U)) { return; }
    n = serial_count < rfcomm_mtu ? serial_count : rfcomm_mtu;
    if (n > sizeof(bytes)) { n = sizeof(bytes); }
    for (i = 0U; i < n; ++i) { bytes[i] = serial_bytes[(serial_head + i) % SERIAL_CAPACITY]; }
    if (!rfcomm_send_frame(rfcomm_dlci, 0xEFU, bytes, n, -1)) { return; }
    serial_head = (uint16_t)((serial_head + n) % SERIAL_CAPACITY);
    serial_count = (uint16_t)(serial_count - n);
    if (cfc_enabled) { outgoing_credits--; }
    BT_SPP_TxBytes += n;
}

void BT_SPP_OnL2CAP(uint16_t cid, const uint8_t *data, uint16_t length)
{
    uint16_t pos = 0U, body_len;
    if ((BT_Connected == 0U) || (data == NULL)) { return; }
    if (cid == L2CAP_CID_SIGNAL)
    {
        while (pos + 4U <= length)
        {
            body_len = rd16(data + pos + 2U);
            if ((uint32_t)pos + 4U + body_len > length) { BT_SPP_Errors++; return; }
            handle_signal(data[pos], data[pos + 1U], data + pos + 4U, body_len);
            pos = (uint16_t)(pos + body_len + 4U);
        }
        if (pos != length) { BT_SPP_Errors++; }
    }
    else if ((cid == BT_SPP_LOCAL_SDP_CID) && (channels[0].state == BT_SPP_OPEN)) { handle_sdp(data, length, &channels[0]); }
    else if ((cid == BT_SPP_LOCAL_RFCOMM_CID) && (channels[1].state == BT_SPP_OPEN)) { handle_rfcomm(data, length); }
}
void BT_SPP_Process(void)
{
    BTTx *pkt;
    USBH_StatusTypeDef result;
    if ((BT_Connected == 0U) || (BT_ConnectionHandle == 0xFFFFU))
    {
        if (link_active) { BT_SPP_Reset(); }
        return;
    }
    if ((!link_active) || (link_handle != BT_ConnectionHandle))
    {
        BT_SPP_Reset();
        link_active = 1U;
        link_handle = BT_ConnectionHandle;
    }
    serial_process();
    if (BT_SPP_SerialOpen && cfc_enabled && incoming_credits <= 2U && qcount < TX_Q_CAPACITY)
    {
        uint16_t available = (uint16_t)((SERIAL_CAPACITY - serial_count) / rfcomm_mtu);
        uint8_t target = (available > 7U) ? 7U : (uint8_t)available;
        if (target > incoming_credits && rfcomm_send_frame(rfcomm_dlci, 0xFFU, NULL, 0U, (int8_t)(target - incoming_credits)))
        { incoming_credits = target; }
    }
    sync_debug();
    if ((qcount == 0U) || (BT_ACL_TX_BufferState != BT_ACL_BUF_READY) || (BT_ACL_TX_State != BT_ACL_TX_IDLE)) { return; }
    pkt = &q[qhead];
    result = BT_ACL_TX_SendL2CAP(pkt->cid, pkt->bytes, pkt->length);
    if (result == USBH_OK)
    {
        qhead = (uint8_t)((qhead + 1U) % TX_Q_CAPACITY);
        qcount--;
        if (pkt->cid == L2CAP_CID_SIGNAL) { BT_L2CAP_SentSignals++; }
    }
    else if (result == USBH_FAIL) { BT_SPP_Errors++; qhead = (uint8_t)((qhead + 1U) % TX_Q_CAPACITY); qcount--; }
    sync_debug();
}
