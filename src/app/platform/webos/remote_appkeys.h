#pragma once

/**
 * Remote app keys on webOS (own variant): read the LG remote's evdev nodes during a
 * stream and act on two streaming-service buttons.
 *
 * - LG Channels (evdev 1043): disconnect, like the overlay's Disconnect (USER_REMOTE_DISCONNECT).
 *   The keyboard comes up by itself on the host's text fields (text_input_rl.c).
 * - Rakuten TV  (evdev 1044): Windows key to the host (USER_REMOTE_WIN_KEY, data1 = down).
 *
 * These keys never reach the app through SDL: the compositor's key filter (LG's, or
 * Glasshouse's) consumes them. The nodes are opened without EVIOCGRAB, so the compositor
 * and other readers (Glasshouse, for Home Assistant) still see every key. Events go to
 * the main thread through SDL_PushEvent, which is thread-safe.
 */

typedef struct remote_appkeys_t remote_appkeys_t;

/** Start the reader thread; returns NULL if no remote node could be opened. */
remote_appkeys_t *remote_appkeys_start(void);

void remote_appkeys_stop(remote_appkeys_t *keys);
