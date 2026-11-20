#include "hints.h"
#include "util/i18n.h"

#include <SDL.h>

/* Own variant: tips for this setup (wired TV, own remote keys, keyboard that follows text fields)
 * instead of Aurora's Wi-Fi, Xbox and ultra-wide tips. Dutch in src/i18n/nl/messages.po. */
#define HINTS_LEN 4
static const char *hints_list[HINTS_LEN] = {
        translatable(
                "Long press BACK (or press EXIT for traditional TV remote) while streaming, to open status overlay."),
        translatable("LG Channels button: disconnect. The game keeps running on the PC."),
        translatable("Rakuten TV button: Windows key."),
        translatable("The keyboard opens by itself when a text field on the PC has focus."),
};

const char *hints_obtain() {
    static int hint_idx = -1;
    if (hint_idx < 0) {
        hint_idx = (int) SDL_GetTicks() % HINTS_LEN;
    } else if (++hint_idx >= HINTS_LEN) {
        hint_idx = 0;
    }

    return locstr(hints_list[hint_idx]);
}