/**
 * idstore.c
 * -----------------------------------------------------------------------------
 * Flash-backed authorised-UID list.
 *
 * Log structured layout:
 *   STM32F3 flash programs in 16-bit half-words and can only clear bits (1->0)
 *   without a full page erase. Never overwrite in place: adding a card
 *   appends to the next empty slot, removing one clears its status word to
 *   "deleted", and only when the page fills do we compact (copy the live
 *   entries out, erase the page once, write them back). This keeps erases rare
 *   and revocation cheap.
 * -----------------------------------------------------------------------------
 */
#include "idstore.h"
#include "main.h"        /* STM32 HAL (flash) */
#include <string.h>

/* Reserve the LAST 2 KB page of the 512 KB device. Code sits at the start of
 * flash and is much smaller than this, so the page is free.
 */
#define STORE_BASE  0x0807F800UL
#define STORE_SIZE  2048U

/* Slot status markers, chosen to respect the flash 1->0 rule:
 *   EMPTY   = erased state (all ones)
 *   ACTIVE  = written once into an empty slot
 *   DELETED = all zero, reachable from ACTIVE by clearing bits (no erase) */
#define SLOT_EMPTY    0xFFFFU
#define SLOT_ACTIVE   0xA5A5U
#define SLOT_DELETED  0x0000U

typedef struct {
    uint16_t status;
    uint8_t  len;
    uint8_t  uid[IDSTORE_UID_MAX];
} id_slot_t;
_Static_assert(sizeof(id_slot_t) == 10, "id_slot_t has unexpected padding");

#define IDSTORE_CAP ((uint16_t)(STORE_SIZE / sizeof(id_slot_t)))   /* 204 slots */

static const id_slot_t *slots(void) { return (const id_slot_t *)STORE_BASE; }

/* ---- Flash hardware seam (the only lines that touch the HAL) -------------- */
static bool flash_write_hw(uint32_t addr, uint16_t hw) {
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, hw) == HAL_OK;
}

static bool flash_erase_store(void) {
    FLASH_EraseInitTypeDef er = {0};
    er.TypeErase   = FLASH_TYPEERASE_PAGES;
    er.PageAddress = STORE_BASE;
    er.NbPages     = 1;
    uint32_t page_error = 0;
    return HAL_FLASHEx_Erase(&er, &page_error) == HAL_OK;
}

/* Program a slot: data half-words first, status LAST -  power loss
 * mid-write leaves the slot empty, never a half-written active entry. */
static bool write_slot(uint16_t index, const uint8_t *uid, uint8_t len) {
    id_slot_t s;
    s.status = SLOT_ACTIVE;
    s.len    = len;
    memset(s.uid, 0, sizeof s.uid);
    memcpy(s.uid, uid, len);

    uint16_t hw[5];
    memcpy(hw, &s, sizeof s);

    uint32_t base = STORE_BASE + (uint32_t)index * sizeof(id_slot_t);
    for (uint8_t i = 1; i < 5; i++) {                 /* data (offsets 2,4,6,8) */
        if (!flash_write_hw(base + (uint32_t)i * 2U, hw[i])) return false;
    }
    return flash_write_hw(base, hw[0]);               /* status commits the slot */
}

static bool same_uid(const id_slot_t *s, const uint8_t *uid, uint8_t len) {
    return s->status == SLOT_ACTIVE && s->len == len && memcmp(s->uid, uid, len) == 0;
}

/* ---- Public interface ---------------------------------------------------- */
bool idstore_init(void) {
    return true;   /* erased flash reads as all-empty: a valid empty list */
}

bool idstore_contains(const uint8_t *uid, uint8_t len) {
    const id_slot_t *s = slots();
    for (uint16_t i = 0; i < IDSTORE_CAP; i++) {
        if (same_uid(&s[i], uid, len)) return true;
    }
    return false;
}

uint16_t idstore_count(void) {
    const id_slot_t *s = slots();
    uint16_t n = 0;
    for (uint16_t i = 0; i < IDSTORE_CAP; i++) {
        if (s[i].status == SLOT_ACTIVE) n++;
    }
    return n;
}

/* Copy the live entries to the front and drop deleted ones. Uses a static
 * buffer (not the stack) because the page is  around 2 KB. Assumes flash is unlocked. */
static bool compact(void) {
    static id_slot_t live[IDSTORE_CAP];
    const id_slot_t *s = slots();
    uint16_t n = 0;
    for (uint16_t i = 0; i < IDSTORE_CAP; i++) {
        if (s[i].status == SLOT_ACTIVE) live[n++] = s[i];
    }
    if (!flash_erase_store()) return false;
    for (uint16_t i = 0; i < n; i++) {
        if (!write_slot(i, live[i].uid, live[i].len)) return false;
    }
    return true;
}

static bool find_empty(uint16_t *out) {
    const id_slot_t *s = slots();
    for (uint16_t i = 0; i < IDSTORE_CAP; i++) {
        if (s[i].status == SLOT_EMPTY) { *out = i; return true; }
    }
    return false;
}

bool idstore_add(const uint8_t *uid, uint8_t len) {
    if (len != 4 && len != 7)        return false;
    if (idstore_contains(uid, len))  return true;     /* already enrolled */

    uint16_t idx;
    HAL_FLASH_Unlock();
    if (!find_empty(&idx)) {                           /* page full of used slots */
        if (!compact() || !find_empty(&idx)) { HAL_FLASH_Lock(); return false; }
    }
    bool ok = write_slot(idx, uid, len);
    HAL_FLASH_Lock();
    return ok;
}

bool idstore_remove(const uint8_t *uid, uint8_t len) {
    const id_slot_t *s = slots();
    for (uint16_t i = 0; i < IDSTORE_CAP; i++) {
        if (same_uid(&s[i], uid, len)) {
            uint32_t addr = STORE_BASE + (uint32_t)i * sizeof(id_slot_t);
            HAL_FLASH_Unlock();
            bool ok = flash_write_hw(addr, SLOT_DELETED);   /* clear status */
            HAL_FLASH_Lock();
            return ok;
        }
    }
    return false;
}

bool idstore_clear_all(void) {
    HAL_FLASH_Unlock();
    bool ok = flash_erase_store();
    HAL_FLASH_Lock();
    return ok;
}
