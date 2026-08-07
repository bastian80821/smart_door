/**
 * nfc.c
 * -----------------------------------------------------------------------------
 * Passage-request wrapper around auth.c.
 * -----------------------------------------------------------------------------
 */
#include "nfc.h"
#include "auth.h"

static bool s_request_active = false;
static bool s_locked = false;

static nfc_result_t s_pending_result = NFC_RESULT_NONE;

void nfc_init(void)
{
    s_request_active = false;
    s_locked = false;
    s_pending_result = NFC_RESULT_NONE;

    auth_set_passage_request(false);
    auth_set_enabled(true);

    /*
     * Clear any old one-shot results.
     */
    (void)auth_access_granted();
    (void)auth_access_denied();
}

void nfc_start_request(void)
{
    s_pending_result = NFC_RESULT_NONE;

    /*
     * Remove stale results from an earlier request.
     */
    (void)auth_access_granted();
    (void)auth_access_denied();

    if (!auth_reader_ready()) {
        s_pending_result = NFC_RESULT_ERROR;
        return;
    }

    s_request_active = true;
    s_locked = false;

    auth_set_enabled(true);
    auth_set_passage_request(true);
}

void nfc_cancel_request(void)
{
    s_request_active = false;
    s_pending_result = NFC_RESULT_NONE;

    auth_set_passage_request(false);

    (void)auth_access_granted();
    (void)auth_access_denied();
}

void nfc_set_locked(bool locked)
{
    s_locked = locked;

    if (locked) {
        s_request_active = false;

        auth_set_passage_request(false);
        auth_set_enabled(false);
    } else {
        auth_set_enabled(true);
    }
}

void nfc_task(void)
{
    if (s_locked ||
        !s_request_active ||
        s_pending_result != NFC_RESULT_NONE) {
        return;
    }

    /*
     * auth_task() is still called from main.c.
     *
     * These accessors read its independent one-shot results without taking the
     * event away from mgmt.c.
     */
    if (auth_access_granted()) {
        s_pending_result = NFC_RESULT_AUTHORIZED;
        s_request_active = false;
        auth_set_passage_request(false);
    }
    else if (auth_access_denied()) {
        s_pending_result = NFC_RESULT_DENIED;
        s_request_active = false;
        auth_set_passage_request(false);
    }
}

nfc_result_t nfc_poll_result(void)
{
    nfc_result_t result = s_pending_result;

    s_pending_result = NFC_RESULT_NONE;

    return result;
}

bool nfc_is_ready(void)
{
    return auth_reader_ready();
}

bool nfc_reinitialize(void)
{
    /*
     * Reuse the existing auth initialisation.
     */
    auth_init();
    nfc_init();

    return auth_reader_ready();
}
