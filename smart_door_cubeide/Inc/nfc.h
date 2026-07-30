/**
 * nfc.h
 * -----------------------------------------------------------------------------
 * Thin passage-request layer around the team's existing auth module.
 *
 * NFC communication and authentication are not duplicated here. auth.c remains
 * responsible for the PN532, UID list, MIFARE verification and admin handling.
 * -----------------------------------------------------------------------------
 */
#ifndef NFC_H
#define NFC_H

#include <stdbool.h>

typedef enum {
    NFC_RESULT_NONE = 0,
    NFC_RESULT_AUTHORIZED,
    NFC_RESULT_DENIED,
    NFC_RESULT_ERROR
} nfc_result_t;

void nfc_init(void);

void nfc_start_request(void);
void nfc_cancel_request(void);

/** Enable or disable all NFC processing. */
void nfc_set_locked(bool locked);

/** Read the one-shot results produced by auth_task(). */
void nfc_task(void);

nfc_result_t nfc_poll_result(void);

bool nfc_is_ready(void);

/** Retry PN532 initialisation during a manual reset. */
bool nfc_reinitialize(void);

#endif /* NFC_H */
