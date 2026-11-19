#pragma once

/**
 * Own variant: the soft keyboard follows the PC's text fields (Rubylight control message 0x553B,
 * "Text input", host setting keyboard_follows_focus, on by default).
 *
 * Once the stream has started the client asks for notices; the host then sends one when a text
 * field gains focus (raise), loses it (lower) or changes (update), numbered by a serial. Raise and
 * update open the soft keyboard, lower closes it (USER_OPEN_SOFT_KEYBOARD / USER_CLOSE_SOFT_KEYBOARD).
 * The host only sends on a change, so a left click on the stream asks again 250 ms later; a request
 * makes the host raise at once if a text field has the focus (same field clicked after the keyboard
 * was closed by hand). Clicks are seen through LiSetMouseButtonHook (patches/core/moonlight-common-c).
 * Wire format: Rubylight client/protocol/core/src/text_input.rs. Hosts without the extension
 * never send a notice, so nothing changes there.
 */

/** Call when the connection has started (control stream up). */
void text_input_rl_start(void);

/** Call when the connection stops or is terminated. */
void text_input_rl_stop(void);
