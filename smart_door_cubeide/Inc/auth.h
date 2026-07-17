/**
 * auth.h
 * -----------------------------------------------------------------------------
 * Access control: reads cards from the PN532, decides grant/deny against the
 * flash-backed ID list, and handles the admin override fob.
 *
 * Admin model: tapping the admin fob opens a short "admin window". While it's
 * open, tapping an unknown card enrols it, and the management menu is reachable
 * (mgmt asks auth_is_admin()). A normal enrolled card is granted entry; an
 * unknown card is denied.
 *
 * Non-blocking: call auth_task() once per main loop.
 * -----------------------------------------------------------------------------
 */
#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>

void auth_init(void);
void auth_task(void);

/* True while the admin window is open (an admin fob was tapped recently).
 * The management module gates its config menu on this. */
bool auth_is_admin(void);

/* One-shot: true for a single read after a valid (enrolled/admin) entry tap,
 * so the door module can act on it. Auto-clears once read. */
bool auth_access_granted(void);

#endif /* AUTH_H */
