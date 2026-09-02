#ifndef  __MESSAGE_H
#define  __MESSAGE_H

#include <stddef.h>

/*
* A message set by display-cli and shown on the display's header line.
*
* The CLI and the display daemon are separate processes, so the message
* passes between them through a small file. The daemon already wakes once
* per screen, which lets it poll that file instead of carrying a socket and
* the accept/partial-read handling that would come with one, and means the
* CLI works whether or not the daemon happens to be running.
*/

/* The 8x16 font fits 19 characters across the 160 pixel display. */
#define DISPLAY_MESSAGE_MAX 20

/* Where the message lives.
 *
 * /run is a tmpfs the kernel starts empty on every boot, so a message
 * lasts as long as the machine is up and no longer. Restarting the display
 * service keeps it; restarting the machine does not.
 *
 * The directory does not survive either, so something has to recreate it
 * each boot. deployment_service.sh installs a systemd-tmpfiles rule that
 * does, owned by whoever deployed the service so that setting a message
 * needs no sudo.
 *
 * Overridable, mainly so this can be exercised without writing to a system
 * directory. */
#define DISPLAY_MESSAGE_ENV  "UCTRONICS_DISPLAY_MESSAGE_FILE"
#define DISPLAY_MESSAGE_PATH "/run/uctronics-display/message"

/* The file currently in use: $UCTRONICS_DISPLAY_MESSAGE_FILE if set,
   DISPLAY_MESSAGE_PATH otherwise. */
const char *display_message_path(void);

/*
* Copy `in` to `out`, keeping only what the display can render.
*
* The font tables cover the 95 printable ASCII characters and are indexed
* as (ch - 32), so anything outside that range reads outside the table.
* Callers must pass display-bound text through here first.
*/
void display_message_sanitize(char *out, size_t length, const char *in);

/* Read the current message. Returns 1 if one is set, 0 if not. */
int display_message_read(char *buffer, size_t length);

/* Set the message. Returns 0 on success, -1 with errno set on failure. */
int display_message_write(const char *text);

/* Remove the message, restoring the default header. Returns 0 on success
   (including when none was set), -1 with errno set on failure. */
int display_message_clear(void);

#endif /*__MESSAGE_H*/
