#include "text_input_rl.h"

#include "logging.h"
#include "util/bus.h"
#include "util/user_event.h"

#include <Limelight.h>

#include <stdbool.h>
#include <stdint.h>

#define TEXT_INPUT_MESSAGE_TYPE 0x553B
#define TEXT_INPUT_VERSION 1
#define TEXT_INPUT_FLAG_NOTICES 0x01
#define TEXT_INPUT_NOTICE_HEADER_BYTES 16

#define NOTICE_LOWER 0
#define NOTICE_RAISE 1
#define NOTICE_UPDATE 2

static volatile bool active;
static bool have_serial;
static uint32_t last_serial;

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
            commons_log_info("TextInput", "Text field focused (type %u, serial %u): open keyboard",
                             input_type, serial);
            bus_pushevent(USER_OPEN_SOFT_KEYBOARD, NULL, NULL);
            break;
        case NOTICE_LOWER:
            commons_log_info("TextInput", "No text field focused (serial %u): close keyboard", serial);
            bus_pushevent(USER_CLOSE_SOFT_KEYBOARD, NULL, NULL);
            break;
        case NOTICE_UPDATE:
        default:
            break;
    }
}

void text_input_rl_start(void) {
    have_serial = false;
    last_serial = 0;
    active = true;
    LiSetUnknownControlMessageCallback(on_control_message);
    /* Request: version u8, flags u8 (bit 0 notices), max_text u16 LE (0: no surrounding text). */
    const unsigned char request[4] = {TEXT_INPUT_VERSION, TEXT_INPUT_FLAG_NOTICES, 0, 0};
    int rc = LiSendControlStreamMessage(TEXT_INPUT_MESSAGE_TYPE, request, sizeof(request));
    commons_log_info("TextInput", "Asked the host for text field notices: %s",
                     rc == 0 ? "sent" : (rc == -2 ? "no ENet control stream" : "send failed"));
}

void text_input_rl_stop(void) {
    active = false;
    LiSetUnknownControlMessageCallback(NULL);
}
