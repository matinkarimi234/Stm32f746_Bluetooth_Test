#include "bt_bonds.h"
#include "bt_port.h"
#include "usbh_bluetooth.h"
#include <string.h>

#define BT_BOND_MAGIC 0x424F4E44UL
#define BT_BOND_JOURNAL_RECORD_BYTES 36U
#define BT_BOND_MAX_JOURNAL_SLOTS (BT_BONDS_STORAGE_BYTES / BT_BOND_JOURNAL_RECORD_BYTES)

typedef struct
{
    uint32_t magic;
    uint32_t seq;
    uint8_t address[6];       /* HCI byte order, not display order */
    uint8_t key[16];
    uint8_t type;
    uint8_t reserved;
    uint32_t crc;
} BT_BondRecord;

typedef char BT_BondRecord_SizeMustBe36[(sizeof(BT_BondRecord) == 36U) ? 1 : -1];
static BT_BondRecord cache[BT_BONDS_MAX_DEVICES];
static uint8_t count = 0U;
static uint8_t initialized = 0U;
static uint8_t storage_fault = 0U;
static uint16_t free_slot = 0U;
static uint32_t next_seq = 1UL;
static uint16_t pending_mask = 0U;

volatile uint8_t BT_Bonds_Count = 0U;
volatile uint8_t BT_Bonds_Dirty = 0U;
volatile uint32_t BT_Bonds_Writes = 0UL;
volatile uint32_t BT_Bonds_FlashErrors = 0UL;
volatile uint32_t BT_Bonds_Reused = 0UL;
volatile uint32_t BT_Bonds_Evictions = 0UL;
volatile uint8_t BT_Bonds_Full = 0U;

static uint32_t BT_Bonds_CRC(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint8_t j;
    for (i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (j = 0U; j < 8U; ++j) { crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U); }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static uint32_t BT_Bonds_Checksum(const BT_BondRecord *r)
{
    return BT_Bonds_CRC((const uint8_t *)&r->seq, 28U);
}

static int8_t BT_Bonds_Index(const uint8_t address[6])
{
    uint8_t i;
    for (i = 0U; i < count; ++i) { if (memcmp(cache[i].address, address, 6U) == 0) { return (int8_t)i; } }
    return -1;
}

static uint8_t BT_Bonds_Validate(const BT_BondRecord *r)
{
    if (r->magic != BT_BOND_MAGIC) { return 0U; }
    if ((r->type < 0x04U) || (r->type > 0x08U)) { return 0U; }
    if (r->crc != BT_Bonds_Checksum(r)) { return 0U; }
    return 1U;
}

void BT_Bonds_Init(void)
{
    BT_BondRecord record;
    const BT_BondRecord *r = &record;
    uint16_t slot;
    int8_t idx;
    uint8_t i;
    uint8_t oldest;
    if (initialized != 0U) { return; }
    initialized = 1U;
    storage_fault = 0U;
    count = 0U;
    free_slot = BT_BOND_MAX_JOURNAL_SLOTS;
    pending_mask = 0U;
    next_seq = 1UL;
    memset(cache, 0, sizeof(cache));
    for (slot = 0U; slot < BT_BOND_MAX_JOURNAL_SLOTS; ++slot)
    {
        if (!BT_Port_BondRead((uint32_t)slot * sizeof(record), &record, sizeof(record)))
        { storage_fault = 1U; BT_Bonds_FlashErrors++; return; }
        if (r->magic == 0xFFFFFFFFUL)
        {
            uint32_t j;
            const uint32_t *words = (const uint32_t *)r;
            uint8_t erased = 1U;
            for (j = 1U; j < 9U; j++) { if (words[j] != 0xFFFFFFFFUL) { erased = 0U; break; } }
            if ((erased != 0U) && (free_slot == BT_BOND_MAX_JOURNAL_SLOTS)) { free_slot = slot; }
            continue;
        }
        if (BT_Bonds_Validate(r) == 0U) { continue; }
        if (r->seq >= next_seq) { next_seq = r->seq + 1UL; }
        idx = BT_Bonds_Index(r->address);
        if (idx >= 0)
        {
            if (r->seq > cache[idx].seq) { memcpy(&cache[idx], r, sizeof(BT_BondRecord)); }
        }
        else if (count < BT_BONDS_MAX_DEVICES)
        {
            memcpy(&cache[count++], r, sizeof(BT_BondRecord));
        }
        else
        {
            oldest = 0U;
            for (i = 1U; i < count; ++i) { if (cache[i].seq < cache[oldest].seq) { oldest = i; } }
            if (r->seq > cache[oldest].seq) { memcpy(&cache[oldest], r, sizeof(BT_BondRecord)); }
        }
    }
    BT_Bonds_Count = count;
    BT_Bonds_Full = (free_slot >= BT_BOND_MAX_JOURNAL_SLOTS) ? 1U : 0U;
}

uint8_t BT_Bonds_Find(const uint8_t address[6], uint8_t key_out[16])
{
    int8_t idx;
    if ((address == NULL) || (key_out == NULL)) { return 0U; }
    BT_Bonds_Init();
    idx = BT_Bonds_Index(address);
    if (idx < 0) { return 0U; }
    memcpy(key_out, cache[(uint8_t)idx].key, 16U);
    BT_Bonds_Reused++;
    return 1U;
}

void BT_Bonds_Remember(const uint8_t address[6], const uint8_t key[16], uint8_t type)
{
    int8_t idx;
    uint8_t i;
    uint8_t oldest;
    BT_BondRecord *r;
    if ((address == NULL) || (key == NULL) || (type < 0x04U) || (type > 0x08U)) { return; }
    BT_Bonds_Init();
    idx = BT_Bonds_Index(address);
    if (idx < 0)
    {
        if (count < BT_BONDS_MAX_DEVICES) { idx = (int8_t)count++; }
        else
        {
            oldest = 0U;
            for (i = 1U; i < count; ++i) { if (cache[i].seq < cache[oldest].seq) { oldest = i; } }
            idx = (int8_t)oldest;
            BT_Bonds_Evictions++;
        }
    }
    else if ((memcmp(cache[(uint8_t)idx].key, key, 16U) == 0) && (cache[(uint8_t)idx].type == type)) { return; }
    r = &cache[(uint8_t)idx];
    memset(r, 0, sizeof(BT_BondRecord));
    r->magic = BT_BOND_MAGIC;
    r->seq = next_seq++;
    memcpy(r->address, address, 6U);
    memcpy(r->key, key, 16U);
    r->type = type;
    r->crc = BT_Bonds_Checksum(r);
    BT_Bonds_Count = count;
    pending_mask |= (uint16_t)(1U << (uint8_t)idx);
    BT_Bonds_Dirty = 1U;
}

/* Write with the magic word LAST: torn power-loss writes are never accepted. */
void BT_Bonds_Process(void)
{
    const BT_BondRecord *r;
    BT_BondRecord candidate;
    uint32_t offset;
    uint8_t i;
    uint8_t ok = 1U;
    uint8_t index = 0xFFU;
    extern volatile uint8_t BT_Connected;
    if (storage_fault || (BT_Bonds_Dirty == 0U) || (BT_Connected != 0U) ||
        (BT_ConnState != BT_CONN_IDLE && BT_ConnState != BT_CONN_FAILED) ||
        (BT_PairCmdState != BT_PAIR_IDLE) ||
        (BT_HCI_CmdState != BT_HCI_CMD_IDLE && BT_HCI_CmdState != BT_HCI_CMD_OK)) { return; }
    for (i = 0U; i < count; ++i) { if ((pending_mask & ((uint16_t)1U << i)) != 0U) { index = i; break; } }
    if (index >= count) { BT_Bonds_Dirty = 0U; return; }
    /* A recovered journal may contain an erased hole followed by valid or
     * torn records. Never assume the slot after that hole is also erased. */
    while (free_slot < BT_BOND_MAX_JOURNAL_SLOTS)
    {
        const uint32_t *words = (const uint32_t *)&candidate;
        if (!BT_Port_BondRead((uint32_t)free_slot * sizeof(candidate), &candidate, sizeof(candidate)))
        { storage_fault = 1U; BT_Bonds_FlashErrors++; return; }
        for (i = 0U; i < 9U; ++i) { if (words[i] != 0xFFFFFFFFUL) { break; } }
        if (i == 9U) { break; }
        free_slot++;
    }
    if (free_slot >= BT_BOND_MAX_JOURNAL_SLOTS) { BT_Bonds_Full = 1U; return; }
    r = &cache[index];
    offset = (uint32_t)free_slot * sizeof(BT_BondRecord);
    ok = BT_Port_BondWriteRecord(offset, r, sizeof(BT_BondRecord));
    if (ok != 0U)
    {
        BT_Bonds_Writes++;
        pending_mask &= (uint16_t)~((uint16_t)1U << index);
        BT_Bonds_Dirty = (pending_mask != 0U);
    }
    else
    {
        BT_Bonds_FlashErrors++;
        /* Retain pending data and retry in a fresh erased slot. */
    }
    free_slot++; /* Never overwrite a partial/torn record. */
    if (free_slot >= BT_BOND_MAX_JOURNAL_SLOTS) { BT_Bonds_Full = 1U; }
}

/* Explicit opt-in factory reset. Do NOT call automatically on normal startup. */
uint8_t BT_Bonds_ClearAll(void)
{
    if ((BT_Connected != 0U) || (BT_ConnState != BT_CONN_IDLE && BT_ConnState != BT_CONN_FAILED) ||
        BT_PairCmdState != BT_PAIR_IDLE || (BT_HCI_CmdState != BT_HCI_CMD_IDLE && BT_HCI_CmdState != BT_HCI_CMD_OK)) { return 0U; }
    if (!BT_Port_BondErase()) { BT_Bonds_FlashErrors++; return 0U; }
    initialized = 0U;
    BT_Bonds_Dirty = 0U;
    pending_mask = 0U;
    BT_Bonds_Init();
    return 1U;
}
