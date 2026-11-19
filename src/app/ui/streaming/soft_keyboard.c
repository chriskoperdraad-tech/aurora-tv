/*
 * Soft keyboard overlay (Windows 11 / Artemis OSK layout) - sends keys to streaming host.
 * Layers: alphabetic (abc) and symbols (&123, optional page 2). Esc + F1-F12 rows.
 */

#include "soft_keyboard.h"
#include "app.h"
#include "config.h"
#include "stream/input/keyboard_input.h"
#include "stream/input/session_input.h"
#include "stream/input/vk.h"
#include "stream/session.h"
#include "lvgl/theme/lv_theme_moonlight_colors.h"

#include <Limelight.h>
#include <stdlib.h>
#include <string.h>

/* Artemis FullKeyboard holds the key for the whole press; match that. */
#define KBD_BTN_W(w) (LV_BTNMATRIX_CTRL_POPOVER | (w))

/* Own variant: calm, modern look (dark, no borders, round keys, soft blue focus). */
#define KBD_KEY_BG         0x2A2D33
#define KBD_KEY_SPECIAL_BG 0x1D2025
#define KBD_KEY_FOCUS_BG   0x383D46
#define KBD_KEY_PRESS      0x454B55
#define KBD_KEY_TEXT       0xE8EAED
#define KBD_PANEL_BG       0x111316
#define KBD_FOCUS_BLUE     0x60A5FA
#define KBD_CHECKED_BG     0x2563EB

typedef enum {
    KBD_LAYER_ALPHA,
    KBD_LAYER_SYMBOLS,
    KBD_LAYER_SYMBOLS2,
} kbd_layer_t;

typedef struct kbd_data_t kbd_data_t;

typedef struct {
    lv_obj_t *container;
    lv_obj_t *btnm;
    lv_group_t *group;
    stream_input_t *input;
    kbd_data_t *kd;
    keyboard_modifier_state_t mods;
    kbd_layer_t layer;
    bool upper_case;
    /* Key held while the button is pressed (Artemis-style press/release). */
    bool key_held;
    uint16_t held_btn_id;
    short held_vk;
    bool held_with_shift;
    void (*on_close)(void *);
    void *on_close_userdata;
} soft_kbd_t;

static soft_kbd_t *g_active_kbd = NULL;

/* Artemis-style d-pad cluster (no spacer row):
 *   …  Home  ^  End
 *   …   <    v   >
 */
#define KBD_ARROW_LEFT  "<\nLB"
#define KBD_ARROW_DOWN  "v"
#define KBD_ARROW_UP    "^"
#define KBD_ARROW_RIGHT ">\nRB"

/* --- Alphabetic, own variant: 4 rows, no Esc/F-row (those are on the &123 pages), so the
 * letters are bigger in the lower keyboard. 12 units per row.
 *   q w e r t y u i o p  Bksp
 *   Tab a s d f g h j k l  Enter
 *   Shift z x c v b n m , .  ^
 *   &123 Ctrl Win Alt  Space  < v >
 */
static const char *alpha_map_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "Bksp",
    "\n",
    "Tab", "a", "s", "d", "f", "g", "h", "j", "k", "l", "Enter",
    "\n",
    "Shift", "z", "x", "c", "v", "b", "n", "m", ",", ".", KBD_ARROW_UP,
    "\n",
    "&123\nLT", "Ctrl", "Win", "Alt", "Space\nY", KBD_ARROW_LEFT, KBD_ARROW_DOWN, KBD_ARROW_RIGHT,
    ""
};

static const char *alpha_map_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "Bksp",
    "\n",
    "Tab", "A", "S", "D", "F", "G", "H", "J", "K", "L", "Enter",
    "\n",
    "Shift", "Z", "X", "C", "V", "B", "N", "M", ",", ".", KBD_ARROW_UP,
    "\n",
    "&123\nLT", "Ctrl", "Win", "Alt", "Space\nY", KBD_ARROW_LEFT, KBD_ARROW_DOWN, KBD_ARROW_RIGHT,
    ""
};

static const lv_btnmatrix_ctrl_t alpha_ctrl[] = {
    /* q..p (10) + Bksp(2) */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, KBD_BTN_W(2),
    /* Tab + a..l (9) + Enter(2) */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, KBD_BTN_W(2),
    /* Shift(2) + z..m (7) + , . + ^ */
    KBD_BTN_W(2), 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* &123(2) Ctrl Win Alt Space(4) < v > */
    KBD_BTN_W(2), 1, 1, 1, KBD_BTN_W(4), 1, 1, 1,
};

/* 11+11+11+8 = 41 */
static const short alpha_vks[] = {
    VK_Q, VK_W, VK_E, VK_R, VK_T, VK_Y, VK_U, VK_I, VK_O, VK_P, VK_BACK,
    VK_TAB, VK_A, VK_S, VK_D, VK_F, VK_G, VK_H, VK_J, VK_K, VK_L, VK_RETURN,
    VK_SHIFT, VK_Z, VK_X, VK_C, VK_V, VK_B, VK_N, VK_M, VK_OEM_COMMA, VK_OEM_PERIOD, VK_UP,
    0, VK_LCONTROL, VK_LWIN, VK_LMENU, VK_SPACE, VK_LEFT, VK_DOWN, VK_RIGHT,
};

/* --- Symbols page 1 (top-row digits VK_1..0 — always type numbers; numpad needs host NumLock) --- */
static const char *sym_map[] = {
    "Esc", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "\n",
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Bksp",
    "\n",
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "'", "\"",
    "\n",
    "1/2", "[", "]", "{", "}", "\\", "|", ";", ":", "Home", KBD_ARROW_UP, "End",
    "\n",
    "abc\nLT", "Ctrl", "Tab", ",", "Space\nY", ".", KBD_ARROW_LEFT, KBD_ARROW_DOWN, KBD_ARROW_RIGHT,
    ""
};

static const lv_btnmatrix_ctrl_t sym_ctrl[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, KBD_BTN_W(2),
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    KBD_BTN_W(2), 1, 1, 1, KBD_BTN_W(3), 1, 1, 1, 1,
};

static const bool sym_shift[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1,
    0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const short sym_vks[] = {
    VK_ESCAPE, VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12,
    VK_1, VK_2, VK_3, VK_4, VK_5, VK_6, VK_7, VK_8, VK_9, VK_0, VK_OEM_MINUS, VK_OEM_PLUS, VK_BACK,
    VK_1, VK_2, VK_3, VK_4, VK_5, VK_6, VK_7, VK_8, VK_9, VK_0, VK_OEM_7, VK_OEM_7,
    0, VK_OEM_4, VK_OEM_6, VK_OEM_4, VK_OEM_6, VK_OEM_5, VK_OEM_5, VK_OEM_1, VK_OEM_1, VK_HOME, VK_UP, VK_END,
    0, VK_LCONTROL, VK_TAB, VK_OEM_COMMA, VK_SPACE, VK_OEM_PERIOD, VK_LEFT, VK_DOWN, VK_RIGHT,
};

/* --- Symbols page 2 --- */
static const char *sym2_map[] = {
    "Esc", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "\n",
    "`", "~", "<", ">", "/", "?", "_", "+", "=", "€", "£", "¥", "Bksp",
    "\n",
    "¡", "¿", "°", "•", "…", "§", "¶", "©", "®", "™", "*", "\\",
    "\n",
    "1/2", "PgUp", "PgDn", "Ins", "Del", "(", ")", "[", "]", "Home", KBD_ARROW_UP, "End",
    "\n",
    "abc\nLT", "Ctrl", "Tab", ",", "Space\nY", ".", KBD_ARROW_LEFT, KBD_ARROW_DOWN, KBD_ARROW_RIGHT,
    ""
};

static const lv_btnmatrix_ctrl_t sym2_ctrl[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, KBD_BTN_W(2),
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    KBD_BTN_W(2), 1, 1, 1, KBD_BTN_W(3), 1, 1, 1, 1,
};

static const bool sym2_shift[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const short sym2_vks[] = {
    VK_ESCAPE, VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12,
    VK_OEM_3, VK_OEM_3, VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2, VK_OEM_2, VK_OEM_MINUS, VK_OEM_PLUS, VK_OEM_PLUS, 0, 0, 0, VK_BACK,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, VK_8, VK_OEM_5,
    0, VK_PRIOR, VK_NEXT, VK_INSERT, VK_DELETE, VK_9, VK_0, VK_OEM_4, VK_OEM_6, VK_HOME, VK_UP, VK_END,
    0, VK_LCONTROL, VK_TAB, VK_OEM_COMMA, VK_SPACE, VK_OEM_PERIOD, VK_LEFT, VK_DOWN, VK_RIGHT,
};

#define ALPHA_VK_COUNT ((int)(sizeof(alpha_vks) / sizeof(alpha_vks[0])))
#define SYM_VK_COUNT   ((int)(sizeof(sym_vks) / sizeof(sym_vks[0])))
#define SYM2_VK_COUNT  ((int)(sizeof(sym2_vks) / sizeof(sym2_vks[0])))

/* Key indices. Own variant: the alphabetic page has its own (4 rows); the symbol pages keep
 * Aurora's (5 rows, Esc/F-row on top, toolbar at 50). */
#define ALPHA_BTN_SHIFT  22
#define ALPHA_BTN_LAYER  33
#define ALPHA_BTN_CTRL   34
#define ALPHA_BTN_WIN    35
#define ALPHA_BTN_ALT    36
#define SYM_BTN_PAGE     38
#define SYM_BTN_LAYER    50
#define SYM_BTN_CTRL     51

struct kbd_data_t {
    soft_kbd_t *kbd;
    const short *vks;
    int vk_count;
};

static void kbd_set_btn_checked(lv_obj_t *btnm, uint16_t idx, bool checked) {
    if (checked) {
        lv_btnmatrix_set_btn_ctrl(btnm, idx, LV_BTNMATRIX_CTRL_CHECKED);
    } else {
        lv_btnmatrix_clear_btn_ctrl(btnm, idx, LV_BTNMATRIX_CTRL_CHECKED);
    }
}

static void kbd_update_modifier_visuals(soft_kbd_t *kbd) {
    lv_obj_t *btnm = kbd->btnm;
    if (!btnm) {
        return;
    }
    if (kbd->layer == KBD_LAYER_ALPHA) {
        kbd_set_btn_checked(btnm, ALPHA_BTN_SHIFT, kbd->mods.shift);
        kbd_set_btn_checked(btnm, ALPHA_BTN_WIN, kbd->mods.win);
        kbd_set_btn_checked(btnm, ALPHA_BTN_ALT, kbd->mods.alt);
        kbd_set_btn_checked(btnm, ALPHA_BTN_CTRL, kbd->mods.ctrl);
    } else {
        kbd_set_btn_checked(btnm, SYM_BTN_CTRL, kbd->mods.ctrl);
    }
}

static const lv_btnmatrix_ctrl_t *kbd_ctrl_map(kbd_layer_t layer, size_t *ctrl_count) {
    if (layer == KBD_LAYER_ALPHA) {
        *ctrl_count = sizeof(alpha_ctrl) / sizeof(alpha_ctrl[0]);
        return alpha_ctrl;
    }
    if (layer == KBD_LAYER_SYMBOLS2) {
        *ctrl_count = sizeof(sym2_ctrl) / sizeof(sym2_ctrl[0]);
        return sym2_ctrl;
    }
    *ctrl_count = sizeof(sym_ctrl) / sizeof(sym_ctrl[0]);
    return sym_ctrl;
}

static void kbd_apply_controls(soft_kbd_t *kbd) {
    lv_obj_t *btnm = kbd->btnm;
    size_t ctrl_count = 0;
    const lv_btnmatrix_ctrl_t *ctrl_map = kbd_ctrl_map(kbd->layer, &ctrl_count);
    lv_btnmatrix_set_ctrl_map(btnm, ctrl_map);
    /* No CLICK_TRIG: VALUE_CHANGED on press is unused; we drive keys from
     * PRESSED/RELEASED so Home/F-keys stay down for the whole touch (Artemis). */
    lv_btnmatrix_set_btn_ctrl_all(btnm, LV_BTNMATRIX_CTRL_NO_REPEAT);
    for (size_t i = 0; i < ctrl_count; i++) {
        if (ctrl_map[i] & LV_BTNMATRIX_CTRL_POPOVER) {
            lv_btnmatrix_set_btn_ctrl(btnm, (uint16_t) i, LV_BTNMATRIX_CTRL_POPOVER);
        }
    }
    kbd_update_modifier_visuals(kbd);
}

static void kbd_bind_vks(kbd_data_t *kd, kbd_layer_t layer) {
    if (layer == KBD_LAYER_SYMBOLS) {
        kd->vks = sym_vks;
        kd->vk_count = SYM_VK_COUNT;
    } else if (layer == KBD_LAYER_SYMBOLS2) {
        kd->vks = sym2_vks;
        kd->vk_count = SYM2_VK_COUNT;
    } else {
        kd->vks = alpha_vks;
        kd->vk_count = ALPHA_VK_COUNT;
    }
}

static void kbd_refresh_map(soft_kbd_t *kbd) {
    if (!kbd->btnm) {
        return;
    }
    if (kbd->layer == KBD_LAYER_SYMBOLS) {
        lv_btnmatrix_set_map(kbd->btnm, sym_map);
    } else if (kbd->layer == KBD_LAYER_SYMBOLS2) {
        lv_btnmatrix_set_map(kbd->btnm, sym2_map);
    } else {
        lv_btnmatrix_set_map(kbd->btnm, kbd->upper_case ? alpha_map_upper : alpha_map_lower);
    }
    kbd_apply_controls(kbd);
    kbd_apply_layer_font(kbd);
}

static void kbd_toggle_layer(soft_kbd_t *kbd, kbd_data_t *kd) {
    if (kbd->layer == KBD_LAYER_ALPHA) {
        kbd->layer = KBD_LAYER_SYMBOLS;
        kbd->upper_case = false;
    } else {
        kbd->layer = KBD_LAYER_ALPHA;
    }
    kbd_bind_vks(kd, kbd->layer);
    kbd_refresh_map(kbd);
}

static void kbd_send_key(soft_kbd_t *kbd, short vk, bool down) {
    keyboard_input_send_key(kbd->input, vk, down, &kbd->mods, app_configuration->syskey_capture);
}

static void kbd_send_key_shift(soft_kbd_t *kbd, short vk, bool down) {
    keyboard_modifier_state_t mods = kbd->mods;
    mods.shift = true;
    keyboard_input_send_key(kbd->input, vk, down, &mods, app_configuration->syskey_capture);
}

static void kbd_force_release(soft_kbd_t *kbd) {
    keyboard_input_force_release_all_modifiers(kbd->input, app_configuration->syskey_capture);
    memset(&kbd->mods, 0, sizeof(kbd->mods));
}

static bool kbd_sym_needs_shift(soft_kbd_t *kbd, uint16_t btn_id) {
    if (kbd->layer == KBD_LAYER_SYMBOLS && btn_id < (uint16_t) SYM_VK_COUNT) {
        return sym_shift[btn_id];
    }
    if (kbd->layer == KBD_LAYER_SYMBOLS2 && btn_id < (uint16_t) SYM2_VK_COUNT) {
        return sym2_shift[btn_id];
    }
    return false;
}

static bool kbd_handle_meta_button(soft_kbd_t *kbd, kbd_data_t *kd, uint16_t btn_id, short vk) {
    if (btn_id == (kbd->layer == KBD_LAYER_ALPHA ? ALPHA_BTN_LAYER : SYM_BTN_LAYER)) {
        kbd_toggle_layer(kbd, kd);
        return true;
    }
    if ((kbd->layer == KBD_LAYER_SYMBOLS || kbd->layer == KBD_LAYER_SYMBOLS2) && btn_id == SYM_BTN_PAGE) {
        kbd->layer = (kbd->layer == KBD_LAYER_SYMBOLS) ? KBD_LAYER_SYMBOLS2 : KBD_LAYER_SYMBOLS;
        kbd_bind_vks(kd, kbd->layer);
        kbd_refresh_map(kbd);
        return true;
    }
    if (vk == 0) {
        return true;
    }
    if (vk == VK_SHIFT) {
        kbd->mods.shift = !kbd->mods.shift;
        keyboard_input_send_modifier(kbd->input, VK_SHIFT, kbd->mods.shift);
        kbd->upper_case = kbd->mods.shift;
        kbd_refresh_map(kbd);
        return true;
    }
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) {
        kbd->mods.ctrl = !kbd->mods.ctrl;
        keyboard_input_send_modifier(kbd->input, VK_LCONTROL, kbd->mods.ctrl);
        kbd_update_modifier_visuals(kbd);
        return true;
    }
    if (vk == VK_LMENU || vk == VK_RMENU) {
        kbd->mods.alt = !kbd->mods.alt;
        keyboard_input_send_modifier(kbd->input, VK_LMENU, kbd->mods.alt);
        kbd_update_modifier_visuals(kbd);
        return true;
    }
    if ((vk == VK_LWIN || vk == VK_RWIN) && app_configuration->syskey_capture) {
        kbd->mods.win = !kbd->mods.win;
        keyboard_input_send_modifier(kbd->input, VK_LWIN, kbd->mods.win);
        kbd_update_modifier_visuals(kbd);
        return true;
    }
    return false;
}

static void kbd_release_held_key(soft_kbd_t *kbd) {
    if (!kbd->key_held) {
        return;
    }
    if (kbd->held_with_shift) {
        kbd_send_key_shift(kbd, kbd->held_vk, false);
    } else {
        kbd_send_key(kbd, kbd->held_vk, false);
    }
    kbd->key_held = false;
    kbd->held_btn_id = LV_BTNMATRIX_BTN_NONE;
    kbd->held_vk = 0;
    kbd->held_with_shift = false;
    const keyboard_modifier_state_t mods_before = kbd->mods;
    keyboard_input_release_toggles(kbd->input, &kbd->mods, app_configuration->syskey_capture);
    if (kbd->layer == KBD_LAYER_ALPHA && kbd->upper_case != kbd->mods.shift) {
        /* set_map rebuilds and redraws all 59 keys: only when the case actually flipped
         * (one-shot Shift released), not on every keystroke. */
        kbd->upper_case = kbd->mods.shift;
        if (kbd->btnm) {
            lv_btnmatrix_set_map(kbd->btnm, kbd->upper_case ? alpha_map_upper : alpha_map_lower);
            kbd_apply_controls(kbd);
        }
    } else if (kbd->btnm && memcmp(&mods_before, &kbd->mods, sizeof(kbd->mods)) != 0) {
        /* One-shot Ctrl/Alt/Win released: repaint only those keys. */
        kbd_update_modifier_visuals(kbd);
    }
}

static void on_keyboard_pressed(lv_event_t *e) {
    kbd_data_t *kd = lv_event_get_user_data(e);
    soft_kbd_t *kbd = kd->kbd;
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
    if (btn_id == LV_BTNMATRIX_BTN_NONE || btn_id >= (uint16_t) kd->vk_count) {
        return;
    }
    if (kbd->key_held) {
        kbd_release_held_key(kbd);
    }

    short vk = kd->vks[btn_id];
    if (kbd_handle_meta_button(kbd, kd, btn_id, vk)) {
        return;
    }

    bool with_shift = kbd_sym_needs_shift(kbd, btn_id);
    if (with_shift) {
        kbd_send_key_shift(kbd, vk, true);
    } else {
        kbd_send_key(kbd, vk, true);
    }
    kbd->key_held = true;
    kbd->held_btn_id = btn_id;
    kbd->held_vk = vk;
    kbd->held_with_shift = with_shift;
}

static void on_keyboard_released(lv_event_t *e) {
    kbd_data_t *kd = lv_event_get_user_data(e);
    kbd_release_held_key(kd->kbd);
}

static void kbd_data_delete_cb(lv_event_t *e) {
    kbd_data_t *kd = lv_event_get_user_data(e);
    if (kd && kd->kbd) {
        kbd_release_held_key(kd->kbd);
        kd->kbd->kd = NULL;
    }
    free(kd);
}

static void container_delete_cb(lv_event_t *e) {
    soft_kbd_t *kbd = lv_event_get_user_data(e);
    if (kbd) {
        if (g_active_kbd == kbd) {
            g_active_kbd = NULL;
        }
        kbd_release_held_key(kbd);
        kbd->btnm = NULL;
        kbd_force_release(kbd);
        stream_input_flush_pressed_keys(kbd->input);
        if (kbd->group) {
            lv_group_del(kbd->group);
        }
        free(kbd);
    }
}

lv_group_t *soft_keyboard_get_group(lv_obj_t *keyboard_container) {
    soft_kbd_t *kbd = lv_obj_get_user_data(keyboard_container);
    return kbd ? kbd->group : NULL;
}

void soft_keyboard_focus_keys(lv_obj_t *keyboard_container) {
    soft_kbd_t *kbd = keyboard_container ? lv_obj_get_user_data(keyboard_container) : NULL;
    if (kbd && kbd->btnm) {
        lv_group_focus_obj(kbd->btnm);
    }
}

bool soft_keyboard_gamepad_shortcut(soft_kbd_gp_action_t action, bool pressed) {
    soft_kbd_t *kbd = g_active_kbd;
    if (!kbd || !kbd->btnm || !kbd->input || !kbd->kd) {
        return false;
    }

    switch (action) {
        case SOFT_KBD_GP_TOGGLE_LAYER:
            if (!pressed) {
                return true;
            }
            kbd_toggle_layer(kbd, kbd->kd);
            return true;
        case SOFT_KBD_GP_ARROW_LEFT:
            kbd_send_key(kbd, VK_LEFT, pressed);
            if (!pressed) {
                keyboard_input_release_toggles(kbd->input, &kbd->mods, app_configuration->syskey_capture);
            }
            return true;
        case SOFT_KBD_GP_ARROW_RIGHT:
            kbd_send_key(kbd, VK_RIGHT, pressed);
            if (!pressed) {
                keyboard_input_release_toggles(kbd->input, &kbd->mods, app_configuration->syskey_capture);
            }
            return true;
        default:
            return false;
    }
}

/* Own variant: the keys that are not characters get a darker fill (alphabetic page). */
static bool kbd_is_special_key(kbd_layer_t layer, uint32_t id) {
    if (layer != KBD_LAYER_ALPHA) {
        return false;
    }
    switch (id) {
        case 10: /* Bksp */
        case 11: /* Tab */
        case 21: /* Enter */
        case ALPHA_BTN_SHIFT:
        case 32: /* ^ */
        case ALPHA_BTN_LAYER:
        case ALPHA_BTN_CTRL:
        case ALPHA_BTN_WIN:
        case ALPHA_BTN_ALT:
        case 38: /* < */
        case 39: /* v */
        case 40: /* > */
            return true;
        default:
            return false;
    }
}

static void kbd_draw_part_cb(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    soft_kbd_t *kbd = lv_event_get_user_data(e);
    if (dsc == NULL || kbd == NULL || dsc->class_p != &lv_btnmatrix_class ||
        dsc->type != LV_BTNMATRIX_DRAW_PART_BTN || dsc->rect_dsc == NULL) {
        return;
    }
    lv_obj_t *btnm = lv_event_get_target(e);
    if (!kbd_is_special_key(kbd->layer, dsc->id) ||
        lv_btnmatrix_has_btn_ctrl(btnm, (uint16_t) dsc->id, LV_BTNMATRIX_CTRL_CHECKED) ||
        (lv_btnmatrix_get_selected_btn(btnm) == dsc->id && lv_obj_has_state(btnm, LV_STATE_FOCUSED))) {
        return;
    }
    dsc->rect_dsc->bg_color = lv_color_hex(KBD_KEY_SPECIAL_BG);
}

static void kbd_apply_layer_font(soft_kbd_t *kbd) {
    if (kbd->btnm == NULL) {
        return;
    }
    /* 4 rows on the alphabetic page leave room for the normal font; the 5-row symbol pages
     * keep the small one so their two-line keys still fit. */
    lv_obj_set_style_text_font(kbd->btnm, kbd->layer == KBD_LAYER_ALPHA
                                          ? lv_theme_get_font_normal(kbd->btnm)
                                          : lv_theme_get_font_small(kbd->btnm), LV_PART_ITEMS);
}

static void kbd_apply_theme(lv_obj_t *btnm) {
    lv_obj_set_style_pad_all(btnm, LV_DPX(4), LV_PART_ITEMS);
    lv_obj_set_style_pad_gap(btnm, LV_DPX(6), 0);
    lv_obj_set_style_radius(btnm, LV_DPX(10), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(btnm, lv_color_hex(KBD_KEY_BG), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnm, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(btnm, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, lv_color_hex(KBD_KEY_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_align(btnm, LV_TEXT_ALIGN_CENTER, LV_PART_ITEMS);

    lv_obj_set_style_bg_color(btnm, lv_color_hex(KBD_KEY_PRESS), LV_PART_ITEMS | LV_STATE_PRESSED);

    /* Focus: lighter fill and a soft blue ring, no border. */
    lv_obj_set_style_bg_color(btnm, lv_color_hex(KBD_KEY_FOCUS_BG), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btnm, lv_color_hex(KBD_KEY_FOCUS_BG), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btnm, 0, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(btnm, 0, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(btnm, lv_color_hex(KBD_FOCUS_BLUE), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(btnm, lv_color_hex(KBD_FOCUS_BLUE), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(btnm, LV_DPX(2), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(btnm, LV_DPX(2), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(btnm, LV_DPX(1), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(btnm, LV_DPX(1), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);

    /* Active Shift/Ctrl/Alt/Win: filled blue. */
    lv_obj_set_style_bg_color(btnm, lv_color_hex(KBD_CHECKED_BG), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btnm, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btnm, lv_color_hex(KBD_CHECKED_BG),
                              LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btnm, lv_color_hex(KBD_CHECKED_BG),
                              LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_FOCUS_KEY);

    /* No outline around the whole matrix. */
    lv_obj_set_style_outline_width(btnm, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(btnm, 0, LV_STATE_FOCUS_KEY);
}

lv_obj_t *soft_keyboard_create(lv_obj_t *parent, session_t *session,
                               void (*on_close_cb)(void *userdata), void *userdata) {
    stream_input_t *input = session ? session_get_input(session) : NULL;

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
    /* Own variant: no dim over the whole screen; the picture now sits above the keyboard
     * (streaming.controller.c, soft_keyboard_place_video) and should stay bright. */
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);

    soft_kbd_t *kbd = malloc(sizeof(soft_kbd_t));
    if (!kbd) {
        return cont;
    }
    memset(kbd, 0, sizeof(*kbd));
    kbd->container = cont;
    kbd->input = input;
    kbd->on_close = on_close_cb;
    kbd->on_close_userdata = userdata;
    kbd->layer = KBD_LAYER_ALPHA;
    kbd->group = lv_group_create();
    lv_obj_set_user_data(cont, kbd);
    g_active_kbd = kbd;

    lv_obj_t *kbd_content = lv_obj_create(cont);
    lv_obj_remove_style_all(kbd_content);
    lv_obj_set_width(kbd_content, LV_PCT(100));
    lv_obj_set_height(kbd_content, lv_pct(SOFT_KBD_HEIGHT_PCT));
    lv_obj_set_style_bg_color(kbd_content, lv_color_hex(KBD_PANEL_BG), 0);
    lv_obj_set_style_bg_opa(kbd_content, LV_OPA_90, 0);
    lv_obj_set_style_radius(kbd_content, LV_DPX(16), 0);
    lv_obj_set_style_pad_hor(kbd_content, LV_DPX(10), 0);
    lv_obj_set_style_pad_ver(kbd_content, LV_DPX(10), 0);
    lv_obj_set_style_pad_gap(kbd_content, LV_DPX(4), 0);
    lv_obj_clear_flag(kbd_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(kbd_content, LV_ALIGN_BOTTOM_MID, 0, 0);

    kbd_data_t *kd = malloc(sizeof(kbd_data_t));
    if (!kd) {
        lv_obj_del(cont);
        return cont;
    }
    kd->kbd = kbd;
    kd->vks = alpha_vks;
    kd->vk_count = ALPHA_VK_COUNT;
    kbd->kd = kd;

    lv_obj_t *btnm = lv_btnmatrix_create(kbd_content);
    kbd->btnm = btnm;
    lv_group_add_obj(kbd->group, btnm);
    lv_btnmatrix_set_map(btnm, alpha_map_lower);
    lv_obj_set_width(btnm, LV_PCT(100));
    lv_obj_set_height(btnm, LV_PCT(100));
    lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btnm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(btnm, lv_theme_get_font_small(parent), 0);
    kbd_apply_theme(btnm);
    kbd_apply_controls(kbd);
    kbd_apply_layer_font(kbd);
    lv_obj_add_event_cb(btnm, kbd_draw_part_cb, LV_EVENT_DRAW_PART_BEGIN, kbd);

    lv_obj_add_event_cb(btnm, on_keyboard_pressed, LV_EVENT_PRESSED, kd);
    lv_obj_add_event_cb(btnm, on_keyboard_released, LV_EVENT_RELEASED, kd);
    lv_obj_add_event_cb(btnm, on_keyboard_released, LV_EVENT_PRESS_LOST, kd);
    lv_obj_add_event_cb(btnm, kbd_data_delete_cb, LV_EVENT_DELETE, kd);
    lv_group_focus_obj(btnm);
    lv_obj_add_event_cb(cont, container_delete_cb, LV_EVENT_DELETE, kbd);

    return cont;
}
