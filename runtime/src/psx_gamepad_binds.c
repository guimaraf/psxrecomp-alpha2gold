/*
 * psx_gamepad_binds.c — configurable Gamepad -> DualShock mappings, INI-driven.
 *
 * Stored in `input.ini` next to the executable (or in platform config directory).
 * Supports Player 1 and Player 2.
 */

#include "psx_gamepad_binds.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define PSXGP_BIT_SELECT   (1u << 0)
#define PSXGP_BIT_L3       (1u << 1)
#define PSXGP_BIT_R3       (1u << 2)
#define PSXGP_BIT_START    (1u << 3)
#define PSXGP_BIT_UP       (1u << 4)
#define PSXGP_BIT_RIGHT    (1u << 5)
#define PSXGP_BIT_DOWN     (1u << 6)
#define PSXGP_BIT_LEFT     (1u << 7)
#define PSXGP_BIT_L2       (1u << 8)
#define PSXGP_BIT_R2       (1u << 9)
#define PSXGP_BIT_L1       (1u << 10)
#define PSXGP_BIT_R1       (1u << 11)
#define PSXGP_BIT_TRIANGLE (1u << 12)
#define PSXGP_BIT_CIRCLE   (1u << 13)
#define PSXGP_BIT_CROSS    (1u << 14)
#define PSXGP_BIT_SQUARE   (1u << 15)

typedef struct {
    const char *name;   /* ini key, e.g. "cross" */
    const char *label;  /* pretty UI label, e.g. "Cross (X)" */
    uint16_t    bit;    /* PSX pad word bit */
} GpButtonDef;

static const GpButtonDef s_btn_defs[PSX_GP_COUNT] = {
    { "up",       "Up",         PSXGP_BIT_UP       },
    { "down",     "Down",       PSXGP_BIT_DOWN     },
    { "left",     "Left",       PSXGP_BIT_LEFT     },
    { "right",    "Right",      PSXGP_BIT_RIGHT    },
    { "cross",    "Cross (X)",  PSXGP_BIT_CROSS    },
    { "circle",   "Circle (O)", PSXGP_BIT_CIRCLE   },
    { "square",   "Square ([])",PSXGP_BIT_SQUARE   },
    { "triangle", "Triangle (/\\)", PSXGP_BIT_TRIANGLE },
    { "l1",       "L1",         PSXGP_BIT_L1       },
    { "r1",       "R1",         PSXGP_BIT_R1       },
    { "l2",       "L2",         PSXGP_BIT_L2       },
    { "r2",       "R2",         PSXGP_BIT_R2       },
    { "l3",       "L3",         PSXGP_BIT_L3       },
    { "r3",       "R3",         PSXGP_BIT_R3       },
    { "start",    "Start",      PSXGP_BIT_START    },
    { "select",   "Select",     PSXGP_BIT_SELECT   },
};

static PsxGamepadBinds s_binds;
static char s_ini_path[1024] = {0};

/* Forward declarations */
static void set_defaults(PsxPlayerGamepadBinds *p);
static void parse_sources_string(const char *val, PsxGamepadBtnBind *btn);
static void source_to_ini_string(const PsxGamepadSource *src, char *buf, size_t sz);
static void source_to_label(const PsxGamepadSource *src, char *buf, size_t sz);

PsxGamepadSource psx_gamepad_source_from_button(SDL_GameControllerButton btn) {
    PsxGamepadSource s;
    s.kind = PSX_GP_SRC_BUTTON;
    s.id = (int)btn;
    return s;
}

PsxGamepadSource psx_gamepad_source_from_axis(SDL_GameControllerAxis axis, int sign) {
    PsxGamepadSource s;
    s.kind = (sign >= 0) ? PSX_GP_SRC_AXIS_POS : PSX_GP_SRC_AXIS_NEG;
    s.id = (int)axis;
    return s;
}

int psx_gamepad_binds_button_count(void) {
    return PSX_GP_COUNT;
}

const char *psx_gamepad_binds_button_name(int button) {
    if (button >= 0 && button < PSX_GP_COUNT) return s_btn_defs[button].name;
    return "";
}

const char *psx_gamepad_binds_button_label(int button) {
    if (button >= 0 && button < PSX_GP_COUNT) return s_btn_defs[button].label;
    return "";
}

static void trim_whitespace(char *s) {
    char *p = s;
    while (isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static void to_lowercase(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static void set_defaults(PsxPlayerGamepadBinds *p) {
    memset(p, 0, sizeof(*p));
    // D-Pad default: dpad button + left stick direction
    p->buttons[PSX_GP_UP].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_DPAD_UP);
    p->buttons[PSX_GP_UP].sources[1] = psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_LEFTY, -1);

    p->buttons[PSX_GP_DOWN].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    p->buttons[PSX_GP_DOWN].sources[1] = psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_LEFTY, 1);

    p->buttons[PSX_GP_LEFT].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    p->buttons[PSX_GP_LEFT].sources[1] = psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_LEFTX, -1);

    p->buttons[PSX_GP_RIGHT].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    p->buttons[PSX_GP_RIGHT].sources[1] = psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_LEFTX, 1);

    // Face buttons
    p->buttons[PSX_GP_CROSS].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_A);
    p->buttons[PSX_GP_CIRCLE].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_B);
    p->buttons[PSX_GP_SQUARE].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_X);
    p->buttons[PSX_GP_TRIANGLE].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_Y);

    // Shoulders
    p->buttons[PSX_GP_L1].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    p->buttons[PSX_GP_R1].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    p->buttons[PSX_GP_L2].sources[0] = psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
    p->buttons[PSX_GP_R2].sources[0] = psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);

    // Sticks
    p->buttons[PSX_GP_L3].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    p->buttons[PSX_GP_R3].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_RIGHTSTICK);

    // System
    p->buttons[PSX_GP_START].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_START);
    p->buttons[PSX_GP_SELECT].sources[0] = psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_BACK);
}

const PsxGamepadBinds *psx_gamepad_binds_get(void) {
    return &s_binds;
}

static PsxGamepadSource parse_single_source(const char *raw) {
    PsxGamepadSource out = { PSX_GP_SRC_NONE, -1 };
    char s[64];
    strncpy(s, raw, sizeof(s) - 1);
    s[sizeof(s) - 1] = '\0';
    trim_whitespace(s);
    to_lowercase(s);

    if (s[0] == '\0' || strcmp(s, "none") == 0 || strcmp(s, "disabled") == 0) return out;

    if (strcmp(s, "a") == 0) return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_A);
    if (strcmp(s, "b") == 0) return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_B);
    if (strcmp(s, "x") == 0) return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_X);
    if (strcmp(s, "y") == 0) return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_Y);
    if (strcmp(s, "back") == 0 || strcmp(s, "view") == 0 || strcmp(s, "select") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_BACK);
    if (strcmp(s, "start") == 0 || strcmp(s, "menu") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_START);
    if (strcmp(s, "guide") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_GUIDE);
    if (strcmp(s, "leftstick") == 0 || strcmp(s, "l3") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    if (strcmp(s, "rightstick") == 0 || strcmp(s, "r3") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    if (strcmp(s, "leftshoulder") == 0 || strcmp(s, "lb") == 0 || strcmp(s, "l1") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    if (strcmp(s, "rightshoulder") == 0 || strcmp(s, "rb") == 0 || strcmp(s, "r1") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    if (strcmp(s, "dpup") == 0 || strcmp(s, "dpadup") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_DPAD_UP);
    if (strcmp(s, "dpdown") == 0 || strcmp(s, "dpaddown") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    if (strcmp(s, "dpleft") == 0 || strcmp(s, "dpadleft") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    if (strcmp(s, "dpright") == 0 || strcmp(s, "dpadright") == 0)
        return psx_gamepad_source_from_button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

    // Axes
    if (strcmp(s, "lefttrigger") == 0 || strcmp(s, "lt") == 0 || strcmp(s, "l2") == 0)
        return psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
    if (strcmp(s, "righttrigger") == 0 || strcmp(s, "rt") == 0 || strcmp(s, "r2") == 0)
        return psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);

    if (strcmp(s, "leftx+") == 0 || strcmp(s, "lsright") == 0)
        return psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_LEFTX, 1);
    if (strcmp(s, "leftx-") == 0 || strcmp(s, "lsleft") == 0)
        return psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_LEFTX, -1);
    if (strcmp(s, "lefty+") == 0 || strcmp(s, "lsdown") == 0)
        return psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_LEFTY, 1);
    if (strcmp(s, "lefty-") == 0 || strcmp(s, "lsup") == 0)
        return psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_LEFTY, -1);

    if (strcmp(s, "rightx+") == 0 || strcmp(s, "rsright") == 0)
        return psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_RIGHTX, 1);
    if (strcmp(s, "rightx-") == 0 || strcmp(s, "rsleft") == 0)
        return psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_RIGHTX, -1);
    if (strcmp(s, "righty+") == 0 || strcmp(s, "rsdown") == 0)
        return psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_RIGHTY, 1);
    if (strcmp(s, "righty-") == 0 || strcmp(s, "rsup") == 0)
        return psx_gamepad_source_from_axis(SDL_CONTROLLER_AXIS_RIGHTY, -1);

    return out;
}

static void parse_sources_string(const char *val, PsxGamepadBtnBind *btn) {
    memset(btn, 0, sizeof(*btn));
    char copy[256];
    strncpy(copy, val, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    int idx = 0;
    char *token = strtok(copy, ",");
    while (token && idx < PSX_GP_MAX_SOURCES) {
        PsxGamepadSource src = parse_single_source(token);
        if (src.kind != PSX_GP_SRC_NONE) {
            btn->sources[idx++] = src;
        }
        token = strtok(NULL, ",");
    }
}

static void source_to_ini_string(const PsxGamepadSource *src, char *buf, size_t sz) {
    if (src->kind == PSX_GP_SRC_NONE) {
        snprintf(buf, sz, "none");
        return;
    }
    if (src->kind == PSX_GP_SRC_BUTTON) {
        switch ((SDL_GameControllerButton)src->id) {
            case SDL_CONTROLLER_BUTTON_A: snprintf(buf, sz, "a"); return;
            case SDL_CONTROLLER_BUTTON_B: snprintf(buf, sz, "b"); return;
            case SDL_CONTROLLER_BUTTON_X: snprintf(buf, sz, "x"); return;
            case SDL_CONTROLLER_BUTTON_Y: snprintf(buf, sz, "y"); return;
            case SDL_CONTROLLER_BUTTON_BACK: snprintf(buf, sz, "back"); return;
            case SDL_CONTROLLER_BUTTON_GUIDE: snprintf(buf, sz, "guide"); return;
            case SDL_CONTROLLER_BUTTON_START: snprintf(buf, sz, "start"); return;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK: snprintf(buf, sz, "leftstick"); return;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK: snprintf(buf, sz, "rightstick"); return;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: snprintf(buf, sz, "leftshoulder"); return;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: snprintf(buf, sz, "rightshoulder"); return;
            case SDL_CONTROLLER_BUTTON_DPAD_UP: snprintf(buf, sz, "dpup"); return;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: snprintf(buf, sz, "dpdown"); return;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: snprintf(buf, sz, "dpleft"); return;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: snprintf(buf, sz, "dpright"); return;
            default: snprintf(buf, sz, "none"); return;
        }
    }
    if (src->kind == PSX_GP_SRC_AXIS_POS) {
        switch ((SDL_GameControllerAxis)src->id) {
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT: snprintf(buf, sz, "lefttrigger"); return;
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: snprintf(buf, sz, "righttrigger"); return;
            case SDL_CONTROLLER_AXIS_LEFTX: snprintf(buf, sz, "leftx+"); return;
            case SDL_CONTROLLER_AXIS_LEFTY: snprintf(buf, sz, "lefty+"); return;
            case SDL_CONTROLLER_AXIS_RIGHTX: snprintf(buf, sz, "rightx+"); return;
            case SDL_CONTROLLER_AXIS_RIGHTY: snprintf(buf, sz, "righty+"); return;
            default: snprintf(buf, sz, "none"); return;
        }
    }
    if (src->kind == PSX_GP_SRC_AXIS_NEG) {
        switch ((SDL_GameControllerAxis)src->id) {
            case SDL_CONTROLLER_AXIS_LEFTX: snprintf(buf, sz, "leftx-"); return;
            case SDL_CONTROLLER_AXIS_LEFTY: snprintf(buf, sz, "lefty-"); return;
            case SDL_CONTROLLER_AXIS_RIGHTX: snprintf(buf, sz, "rightx-"); return;
            case SDL_CONTROLLER_AXIS_RIGHTY: snprintf(buf, sz, "righty-"); return;
            default: snprintf(buf, sz, "none"); return;
        }
    }
    snprintf(buf, sz, "none");
}

static void source_to_label(const PsxGamepadSource *src, char *buf, size_t sz) {
    if (src->kind == PSX_GP_SRC_NONE) {
        snprintf(buf, sz, "None");
        return;
    }
    if (src->kind == PSX_GP_SRC_BUTTON) {
        switch ((SDL_GameControllerButton)src->id) {
            case SDL_CONTROLLER_BUTTON_A: snprintf(buf, sz, "A (Cross)"); return;
            case SDL_CONTROLLER_BUTTON_B: snprintf(buf, sz, "B (Circle)"); return;
            case SDL_CONTROLLER_BUTTON_X: snprintf(buf, sz, "X (Square)"); return;
            case SDL_CONTROLLER_BUTTON_Y: snprintf(buf, sz, "Y (Triangle)"); return;
            case SDL_CONTROLLER_BUTTON_BACK: snprintf(buf, sz, "Back / View"); return;
            case SDL_CONTROLLER_BUTTON_GUIDE: snprintf(buf, sz, "Guide"); return;
            case SDL_CONTROLLER_BUTTON_START: snprintf(buf, sz, "Start / Menu"); return;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK: snprintf(buf, sz, "L3 (Left Stick)"); return;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK: snprintf(buf, sz, "R3 (Right Stick)"); return;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: snprintf(buf, sz, "LB / L1"); return;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: snprintf(buf, sz, "RB / R1"); return;
            case SDL_CONTROLLER_BUTTON_DPAD_UP: snprintf(buf, sz, "D-Pad Up"); return;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: snprintf(buf, sz, "D-Pad Down"); return;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: snprintf(buf, sz, "D-Pad Left"); return;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: snprintf(buf, sz, "D-Pad Right"); return;
            default: snprintf(buf, sz, "Button %d", src->id); return;
        }
    }
    if (src->kind == PSX_GP_SRC_AXIS_POS) {
        switch ((SDL_GameControllerAxis)src->id) {
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT: snprintf(buf, sz, "LT / L2"); return;
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: snprintf(buf, sz, "RT / R2"); return;
            case SDL_CONTROLLER_AXIS_LEFTX: snprintf(buf, sz, "LS Right"); return;
            case SDL_CONTROLLER_AXIS_LEFTY: snprintf(buf, sz, "LS Down"); return;
            case SDL_CONTROLLER_AXIS_RIGHTX: snprintf(buf, sz, "RS Right"); return;
            case SDL_CONTROLLER_AXIS_RIGHTY: snprintf(buf, sz, "RS Down"); return;
            default: snprintf(buf, sz, "Axis %d+", src->id); return;
        }
    }
    if (src->kind == PSX_GP_SRC_AXIS_NEG) {
        switch ((SDL_GameControllerAxis)src->id) {
            case SDL_CONTROLLER_AXIS_LEFTX: snprintf(buf, sz, "LS Left"); return;
            case SDL_CONTROLLER_AXIS_LEFTY: snprintf(buf, sz, "LS Up"); return;
            case SDL_CONTROLLER_AXIS_RIGHTX: snprintf(buf, sz, "RS Left"); return;
            case SDL_CONTROLLER_AXIS_RIGHTY: snprintf(buf, sz, "RS Up"); return;
            default: snprintf(buf, sz, "Axis %d-", src->id); return;
        }
    }
    snprintf(buf, sz, "None");
}

void psx_gamepad_binds_get_label(int player, int button, char *buf, size_t buf_size) {
    if (buf_size == 0) return;
    buf[0] = '\0';
    if (button < 0 || button >= PSX_GP_COUNT) {
        snprintf(buf, buf_size, "None");
        return;
    }
    const PsxPlayerGamepadBinds *p = (player == 2) ? &s_binds.p2 : &s_binds.p1;
    const PsxGamepadBtnBind *btn = &p->buttons[button];

    char s1[64] = {0}, s2[64] = {0};
    source_to_label(&btn->sources[0], s1, sizeof(s1));
    if (btn->sources[1].kind != PSX_GP_SRC_NONE) {
        source_to_label(&btn->sources[1], s2, sizeof(s2));
        snprintf(buf, buf_size, "%s, %s", s1, s2);
    } else {
        snprintf(buf, buf_size, "%s", s1);
    }
}

static int sources_equal(const PsxGamepadSource *a, const PsxGamepadSource *b) {
    return a->kind == b->kind && a->id == b->id;
}

void psx_gamepad_binds_set_source(int player, int button, PsxGamepadSource src) {
    if (button < 0 || button >= PSX_GP_COUNT) return;
    PsxPlayerGamepadBinds *p = (player == 2) ? &s_binds.p2 : &s_binds.p1;

    // Steal: if this source was bound to another button on this player, remove it
    if (src.kind != PSX_GP_SRC_NONE) {
        for (int b = 0; b < PSX_GP_COUNT; b++) {
            if (b == button) continue;
            for (int i = 0; i < PSX_GP_MAX_SOURCES; i++) {
                if (sources_equal(&p->buttons[b].sources[i], &src)) {
                    p->buttons[b].sources[i].kind = PSX_GP_SRC_NONE;
                    p->buttons[b].sources[i].id = -1;
                }
            }
        }
    }

    p->buttons[button].sources[0] = src;
    p->buttons[button].sources[1].kind = PSX_GP_SRC_NONE;
    p->buttons[button].sources[1].id = -1;

    psx_gamepad_binds_save();
}

void psx_gamepad_binds_reset_player(int player) {
    PsxPlayerGamepadBinds *p = (player == 2) ? &s_binds.p2 : &s_binds.p1;
    set_defaults(p);
    psx_gamepad_binds_save();
}

void psx_gamepad_binds_init(const char *exe_path) {
    set_defaults(&s_binds.p1);
    set_defaults(&s_binds.p2);

    if (!exe_path || !exe_path[0]) {
        snprintf(s_ini_path, sizeof(s_ini_path), "input.ini");
    } else {
        char buf[1024];
        strncpy(buf, exe_path, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *slash = strrchr(buf, '/');
        char *bslash = strrchr(buf, '\\');
        char *last = (slash > bslash) ? slash : bslash;
        if (last) {
            *(last + 1) = '\0';
            snprintf(s_ini_path, sizeof(s_ini_path), "%sinput.ini", buf);
        } else {
            snprintf(s_ini_path, sizeof(s_ini_path), "input.ini");
        }
    }

    FILE *f = fopen(s_ini_path, "r");
    if (!f) {
        psx_gamepad_binds_save();
        return;
    }

    char line[512];
    int section = 0; /* 0=none, 1=mapping, 2=player1, 3=player2 */
    while (fgets(line, sizeof(line), f)) {
        char *comment = strpbrk(line, ";#");
        if (comment) *comment = '\0';
        trim_whitespace(line);
        if (line[0] == '\0') continue;

        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            line[strlen(line) - 1] = '\0';
            char *sec = line + 1;
            trim_whitespace(sec);
            to_lowercase(sec);
            if (strcmp(sec, "player1") == 0) section = 2;
            else if (strcmp(sec, "player2") == 0) section = 3;
            else if (strcmp(sec, "mapping") == 0) section = 1;
            else section = 0;
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim_whitespace(key);
        trim_whitespace(val);
        to_lowercase(key);

        PsxPlayerGamepadBinds *target = NULL;
        if (section == 1 || section == 2) target = &s_binds.p1;
        else if (section == 3) target = &s_binds.p2;

        if (!target) continue;

        for (int b = 0; b < PSX_GP_COUNT; b++) {
            if (strcmp(key, s_btn_defs[b].name) == 0) {
                parse_sources_string(val, &target->buttons[b]);
                break;
            }
        }
    }
    fclose(f);
}

void psx_gamepad_binds_save(void) {
    if (!s_ini_path[0]) return;
    FILE *f = fopen(s_ini_path, "w");
    if (!f) return;

    fprintf(f, "; PSXRecomp input mapping.\n");
    fprintf(f, "; Configured live from the launcher Gamepad tab.\n\n");
    fprintf(f, "[controller]\n");
    fprintf(f, "enabled = true\n");
    fprintf(f, "device = 0\n");
    fprintf(f, "deadzone = 12000\n\n");

    // Write legacy [mapping] for backward compatibility with older tools
    fprintf(f, "[mapping]\n");
    for (int b = 0; b < PSX_GP_COUNT; b++) {
        char s1[64] = {0}, s2[64] = {0};
        source_to_ini_string(&s_binds.p1.buttons[b].sources[0], s1, sizeof(s1));
        if (s_binds.p1.buttons[b].sources[1].kind != PSX_GP_SRC_NONE) {
            source_to_ini_string(&s_binds.p1.buttons[b].sources[1], s2, sizeof(s2));
            fprintf(f, "%s = %s,%s\n", s_btn_defs[b].name, s1, s2);
        } else {
            fprintf(f, "%s = %s\n", s_btn_defs[b].name, s1);
        }
    }
    fprintf(f, "\n");

    // Write [player1]
    fprintf(f, "[player1]\n");
    for (int b = 0; b < PSX_GP_COUNT; b++) {
        char s1[64] = {0}, s2[64] = {0};
        source_to_ini_string(&s_binds.p1.buttons[b].sources[0], s1, sizeof(s1));
        if (s_binds.p1.buttons[b].sources[1].kind != PSX_GP_SRC_NONE) {
            source_to_ini_string(&s_binds.p1.buttons[b].sources[1], s2, sizeof(s2));
            fprintf(f, "%s = %s,%s\n", s_btn_defs[b].name, s1, s2);
        } else {
            fprintf(f, "%s = %s\n", s_btn_defs[b].name, s1);
        }
    }
    fprintf(f, "\n");

    // Write [player2]
    fprintf(f, "[player2]\n");
    for (int b = 0; b < PSX_GP_COUNT; b++) {
        char s1[64] = {0}, s2[64] = {0};
        source_to_ini_string(&s_binds.p2.buttons[b].sources[0], s1, sizeof(s1));
        if (s_binds.p2.buttons[b].sources[1].kind != PSX_GP_SRC_NONE) {
            source_to_ini_string(&s_binds.p2.buttons[b].sources[1], s2, sizeof(s2));
            fprintf(f, "%s = %s,%s\n", s_btn_defs[b].name, s1, s2);
        } else {
            fprintf(f, "%s = %s\n", s_btn_defs[b].name, s1);
        }
    }

    fclose(f);
}

static int is_source_pressed(SDL_GameController *pad, const PsxGamepadSource *src, int suppress_stick_axes) {
    if (!pad || src->kind == PSX_GP_SRC_NONE) return 0;

    if (src->kind == PSX_GP_SRC_BUTTON) {
        return SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)src->id);
    }

    if (src->kind == PSX_GP_SRC_AXIS_POS) {
        SDL_GameControllerAxis axis = (SDL_GameControllerAxis)src->id;
        if (suppress_stick_axes && (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY ||
                                   axis == SDL_CONTROLLER_AXIS_RIGHTX || axis == SDL_CONTROLLER_AXIS_RIGHTY)) {
            return 0;
        }
        int16_t val = SDL_GameControllerGetAxis(pad, axis);
        int16_t threshold = (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
            ? 8000 : 16000;
        return (val > threshold);
    }

    if (src->kind == PSX_GP_SRC_AXIS_NEG) {
        SDL_GameControllerAxis axis = (SDL_GameControllerAxis)src->id;
        if (suppress_stick_axes && (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY ||
                                   axis == SDL_CONTROLLER_AXIS_RIGHTX || axis == SDL_CONTROLLER_AXIS_RIGHTY)) {
            return 0;
        }
        int16_t val = SDL_GameControllerGetAxis(pad, axis);
        return (val < -16000);
    }

    return 0;
}

uint16_t psx_gamepad_binds_pad_word(SDL_GameController *pad, int player, int suppress_stick_axes) {
    uint16_t word = 0xFFFF;
    if (!pad) return word;

    const PsxPlayerGamepadBinds *p = (player == 2) ? &s_binds.p2 : &s_binds.p1;
    for (int b = 0; b < PSX_GP_COUNT; b++) {
        for (int i = 0; i < PSX_GP_MAX_SOURCES; i++) {
            if (is_source_pressed(pad, &p->buttons[b].sources[i], suppress_stick_axes)) {
                word &= (uint16_t)~s_btn_defs[b].bit;
                break;
            }
        }
    }
    return word;
}

int psx_gamepad_binds_dpad_active(SDL_GameController *pad, int player) {
    if (!pad) return 0;
    const PsxPlayerGamepadBinds *p = (player == 2) ? &s_binds.p2 : &s_binds.p1;
    for (int b = 0; b < 4; b++) { // UP, DOWN, LEFT, RIGHT
        for (int i = 0; i < PSX_GP_MAX_SOURCES; i++) {
            if (is_source_pressed(pad, &p->buttons[b].sources[i], 0)) return 1;
        }
    }
    return 0;
}
