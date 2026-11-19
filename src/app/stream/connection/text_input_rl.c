#include "text_input_rl.h"

#include "logging.h"
#include "util/bus.h"
#include "util/user_event.h"

#include <Limelight.h>
#include <SDL.h>

#include <stdbool.h>
#include <stdint.h>

#define TEXT_INPUT_MESSAGE_TYPE 0x553B
#define TEXT_INPUT_VERSION 1
#define TEXT_INPUT_FLAG_NOTICES 0x01
#define TEXT_INPUT_NOTICE_HEADER_BYTES 16

#define NOTICE_LOWER 0
#define NOTICE_RAISE 1
#define NOTICE_UPDATE 2

/* After a left click on the stream, ask again this much later, once the PC has moved the focus.
 * The host answers a request at once with a raise if a text field has the focus, so a click in a
 * field brings the keyboard back even when the focus did not change (keyboard closed by hand,
 * same field clicked again). */
#define REASK_DELAY_MS 250

static volatile bool active;
static bool have_serial;
static uint32_t last_serial;
static SDL_atomic_t reask_pending;
static SDL_TimerID reask_timer;

static void send_request(const char *why);

/* Serial numbers wrap; a notice is newer when it is ahead by less than half the range. */
static bool is_newer(uint32_t serial) {
    return !have_serial || (int32_t) (serial - last_serial) > 0;
}

/* Control stream receive thread: keep it short and hand off to the main thread. */
static void on_control_message(unsigned short type, const unsigned char *payload, int length) {
    if (!active || type != TEXT_INPUT_MESSAGE_TYPE) {
        return;
    }
    if (payload == NULL || length < TEXT_INPUT_NOTICE_HEADER_BYTES || payload[0] == 0) {
        commons_log_warn("TextInput", "Ignoring short or invalid notice (%d bytes)", length);
        return;
    }
    uint8_t kind = payload[1];
    uint8_t input_type = payload[2];
    uint32_t serial = (uint32_t) payload[4] | ((uint32_t) payload[5] << 8) |
                      ((uint32_t) payload[6] << 16) | ((uint32_t) payload[7] << 24);
    if (!is_newer(serial)) {
        return;
    }
    have_serial = true;
    last_serial = serial;
    switch (kind) {
        case NOTICE_RAISE:
        case NOTICE_UPDATE:
            /* An update is another field (or its context) while the host thinks it is raised;
             * the keyboard may have been closed by hand since, so open it as for a raise. */
            commons_log_info("TextInput", "Text field focused (kind %u, type %u, serial %u): open keyboard",
                             kind, input_type, serial);
            bus_pushevent(USER_OPEN_SOFT_KEYBOARD, NULL, NULL);
            break;
        case NOTICE_LOWER:
            commons_log_info("TextInput", "No text field focused (serial %u): close keyboard", serial);
            bus_pushevent(USER_CLOSE_SOFT_KEYBOARD, NULL, NULL);
            break;
        default:
            break;
    }
}

/* SDL timer thread. */
static Uint32 reask_timer_cb(Uint32 interval, void *param) {
    (void) interval;
    (void) param;
    SDL_AtomicSet(&reask_pending, 0);
    if (active) {
        send_request("after a click");
    }
    return 0;
}

/* Caller's thread (main or input), from LiSendMouseButtonEvent. */
static void on_mouse_button(char action, int button) {
    if (!active || action != BUTTON_ACTION_PRESS || button != BUTTON_LEFT) {
        return;
    }
    if (SDL_AtomicCAS(&reask_pending, 0, 1)) {
        reask_timer = SDL_AddTimer(REASK_DELAY_MS, reask_timer_cb, NULL);
        if (reask_timer == 0) {
            SDL_AtomicSet(&reask_pending, 0);
        }
    }
}

static void send_request(const char *why) {
    /* Request: version u8, flags u8 (bit 0 notices), max_text u16 LE (0: no surrounding text). */
    const unsigned char request[4] = {TEXT_INPUT_VERSION, TEXT_INPUT_FLAG_NOTICES, 0, 0};
    int rc = LiSendControlStreamMessage(TEXT_INPUT_MESSAGE_TYPE, request, sizeof(request));
    commons_log_info("TextInput", "Asked the host for text field notices (%s): %s", why,
                     rc == 0 ? "sent" : (rc == -2 ? "no ENet control stream" : "send failed"));
}

void text_input_rl_start(void) {
    have_serial = false;
    last_serial = 0;
    active = true;
    SDL_AtomicSet(&reask_pending, 0);
    LiSetUnknownControlMessageCallback(on_control_message);
    LiSetMouseButtonHook(on_mouse_button);
    send_request("stream started");
}

void text_input_rl_stop(void) {
    active = false;
    LiSetMouseButtonHook(NULL);
    LiSetUnknownControlMessageCallback(NULL);
    if (SDL_AtomicGet(&reask_pending) && reask_timer != 0) {
        SDL_RemoveTimer(reask_timer);
    }
    SDL_AtomicSet(&reask_pending, 0);
    reask_timer = 0;
}
