/**
 * idstore.h
 * -----------------------------------------------------------------------------
 * Persistent list of authorised card UIDs, kept in a reserved flash page so
 * enrolments survive power cycles. The admin adds and removes cards at runtime.
 * Card verification with sector encryption happens in the auth module.
 *
 * UIDs are 4 bytes (MIFARE Classic S50) or 7 bytes (some cards/fobs).
 * -----------------------------------------------------------------------------
 */
#ifndef IDSTORE_H
#define IDSTORE_H

#include <stdint.h>
#include <stdbool.h>

#define IDSTORE_UID_MAX 7

bool     idstore_init(void);                                   /* prepare the store */
bool     idstore_contains(const uint8_t *uid, uint8_t len);    /* is UID authorised? */
bool     idstore_add(const uint8_t *uid, uint8_t len);         /* enrol a card       */
bool     idstore_remove(const uint8_t *uid, uint8_t len);      /* revoke a card      */
uint16_t idstore_count(void);                                  /* active entries     */
bool     idstore_clear_all(void);                              /* wipe the list      */

#endif /* IDSTORE_H */
