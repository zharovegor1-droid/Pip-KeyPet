/* The menu's apps. Each one builds its screen in open() and cleans its timers in close().
   Colours, fonts, the clock face and the meters come from the current theme (theme.h). */
#include "doodle.h"
#include "mic.h"
#include "phone.h"
#include "pip.h"
#include "profile.h"
#include "shell.h"
#include "theme.h"
#include "voice.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SCR 466

static lv_obj_t *dot(lv_obj_t *parent, int x, int y, int size, uint32_t color)
{
    return th_box(parent, x - size / 2, y - size / 2, size, size, LV_RADIUS_CIRCLE, color);
}

static void recolor(lv_obj_t *o, uint32_t color) { lv_obj_set_style_bg_color(o, lv_color_hex(color), 0); }

static struct tm local_now(void)
{
    time_t now = (time_t)shell_local_time();
    struct tm tm;
    gmtime_r(&now, &tm);
    return tm;
}

static const char *const WD[] = {"вс", "пн", "вт", "ср", "чт", "пт", "сб"};
static const char *const MON[] = {"янв", "фев", "мар", "апр", "мая", "июн", "июл", "авг", "сен", "окт", "ноя", "дек"};

/* ================= clock: one face per theme ================= */

#define MCOLS 25 /* HH:MM = 5+1+5+1+1+1+5+1+5 */

/* a word for how he is, for the clock */
static const char *pip_word(const pip_info_t *in) { return in->asleep ? "спит" : "спокоен"; }

static struct {
    lv_obj_t *cells[MCOLS * 7], *ring[60], *date, *info, *window, *hands[3], *arcs[3], *comp[2];
    lv_obj_t *grid, *rows[3]; /* the terminal face: one object for all the cells, three data rows */
    th_cell_t tc[MCOLS * 7 + 60];
    int nd;
    lv_point_precise_t pts[3][2], ticks[60][2];
    lv_timer_t *timer;
} ck;

static void matrix_set(int hh, int mm)
{
    int digits[4] = {hh / 10, hh % 10, mm / 10, mm % 10}, starts[4] = {0, 6, 14, 20};
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < MCOLS; c++) {
            bool on = (c == 12 && (r == 2 || r == 4)); /* the colon */
            for (int d = 0; d < 4; d++)
                if (c >= starts[d] && c < starts[d] + 5) on = th_glyph[digits[d]][r][c - starts[d]] == '1';
            recolor(ck.cells[r * MCOLS + c], on ? th->text : th->faint);
        }
}

static void clock_tick(lv_timer_t *t)
{
    (void)t;
    struct tm tm = local_now();
    pip_info_t in;
    pip_get_info(&in);
    if (th->clock == CLOCK_DOTS) matrix_set(tm.tm_hour, tm.tm_min);
    if (th->clock == CLOCK_DOTS) {
        for (int i = 0; i < 60; i++) recolor(ck.ring[i], i == tm.tm_sec ? th->accent : i < tm.tm_sec ? th->dim : th->faint);
        char d[32];
        snprintf(d, sizeof d, "%s %d %s", WD[tm.tm_wday], tm.tm_mday, MON[tm.tm_mon]);
        lv_label_set_text(ck.date, th_label(d));
    } else if (th->clock == CLOCK_BLOCKS) {
        char t[48];
        snprintf(t, sizeof t, "%02d:%02d", tm.tm_hour, tm.tm_min);
        th_blocks_show(ck.grid, ck.tc, t, th->text);
        for (int i = 0; i < 60; i++) th_cell_set(ck.grid, &ck.tc[ck.nd + i], i < tm.tm_sec ? th->text : th->off); /* a cell a second */
        snprintf(t, sizeof t, "%s  %d %s %d", WD[tm.tm_wday], tm.tm_mday, MON[tm.tm_mon], tm.tm_year + 1900);
        lv_label_set_text(ck.date, th_label(t));
        snprintf(t, sizeof t, "%d", in.steps);
        th_leader(ck.rows[0], "шаги", t, 202);
        th_leader(ck.rows[1], "заряд", shell_battery_text(), 202);
        th_leader(ck.rows[2], "пип", th_label(pip_word(&in)), 202);
    } else if (th->clock == CLOCK_DIAL) {
        float ang[3] = {(tm.tm_hour % 12 + tm.tm_min / 60.0f) * 30, (tm.tm_min + tm.tm_sec / 60.0f) * 6, tm.tm_sec * 6.0f};
        const float len[3] = {112, 168, 186}, tail[3] = {16, 20, 36};
        for (int i = 0; i < 3; i++) {
            float a = ang[i] * 3.14159f / 180, sx = sinf(a), cy = -cosf(a);
            ck.pts[i][0] = (lv_point_precise_t){SCR / 2 - sx * tail[i], SCR / 2 - cy * tail[i]};
            ck.pts[i][1] = (lv_point_precise_t){SCR / 2 + sx * len[i], SCR / 2 + cy * len[i]};
            lv_line_set_points(ck.hands[i], ck.pts[i], 2);
        }
        lv_label_set_text_fmt(ck.window, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else { /* CLOCK_BIG */
        lv_label_set_text_fmt(ck.info, "%02d:%02d", tm.tm_hour, tm.tm_min);
        char d[24];
        snprintf(d, sizeof d, "%s %d", WD[tm.tm_wday], tm.tm_mday);
        lv_label_set_text(ck.date, d);
        /* the rings: the battery, the day's steps out of 10 000, the minute */
        int bat = shell_battery_level(NULL);
        float v[3] = {bat < 0 ? 0 : bat / 100.0f, fminf(1, in.steps / 10000.0f), tm.tm_sec / 60.0f};
        for (int i = 0; i < 3; i++) lv_arc_set_value(ck.arcs[i], (int32_t)(v[i] * 100));
        lv_label_set_text_fmt(ck.comp[0], "%d", in.steps);
        lv_label_set_text(ck.comp[1], shell_battery_text());
    }
}

static void matrix_make(lv_obj_t *scr, int y0, bool round)
{
    const int pitch = 15, size = round ? 11 : 12, x0 = (SCR - MCOLS * pitch) / 2;
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < MCOLS; c++)
            ck.cells[r * MCOLS + c] = th_box(scr, x0 + c * pitch, y0 + r * pitch, size, size, round ? LV_RADIUS_CIRCLE : 0, th->faint);
}

static lv_obj_t *ring_arc(lv_obj_t *scr, int size, int width, uint32_t color, int y)
{
    lv_obj_t *a = lv_arc_create(scr);
    lv_obj_set_size(a, size, size);
    lv_obj_align(a, LV_ALIGN_TOP_MID, 0, y - size / 2);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_rotation(a, 270);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

static void clock_open(lv_obj_t *scr)
{
    if (th->clock == CLOCK_DOTS) {
        for (int i = 0; i < 60; i++) {
            float a = i * 6 * 3.14159f / 180;
            ck.ring[i] = dot(scr, (int)(SCR / 2 + sinf(a) * 212), (int)(SCR / 2 - cosf(a) * 212), i % 5 ? 6 : 10, th->faint);
        }
        matrix_make(scr, 168, true);
        ck.date = th_text(scr, th->small, th->dim, "");
        lv_obj_align(ck.date, LV_ALIGN_CENTER, 0, 92);
    } else if (th->clock == CLOCK_BLOCKS) {
        th_header(scr, "пип-01 // время", 52);
        ck.date = th_text_at(scr, th->tiny, th->dim, "", -1, 125);
        ck.nd = th_blocks(ck.tc, 97, 163, "00:00", 11, 9);
        th_ring(ck.tc + ck.nd, 60, 214, 8);
        ck.grid = th_cells(scr, ck.tc, ck.nd + 60);
        for (int i = 0; i < 3; i++) ck.rows[i] = th_text_at(scr, th->tiny, th->text, "", 132, 263 + i * 26);
        th_footer(scr, TH_DOWN " назад", 363);
    } else if (th->clock == CLOCK_DIAL) {
        for (int i = 0; i < 60; i++) {
            float a = i * 6 * 3.14159f / 180, r0 = i % 5 ? 202 : 186;
            ck.ticks[i][0] = (lv_point_precise_t){SCR / 2 + sinf(a) * r0, SCR / 2 - cosf(a) * r0};
            ck.ticks[i][1] = (lv_point_precise_t){SCR / 2 + sinf(a) * 214, SCR / 2 - cosf(a) * 214};
            lv_obj_t *l = lv_line_create(scr);
            lv_line_set_points(l, ck.ticks[i], 2);
            lv_obj_set_style_line_width(l, i % 5 ? 2 : 4, 0);
            lv_obj_set_style_line_color(l, lv_color_hex(i % 15 == 0 ? th->accent : i % 5 ? th->faint : th->text), 0);
        }
        static const char *const nums[4] = {"12", "3", "6", "9"};
        for (int i = 0; i < 4; i++) {
            float a = i * 90 * 3.14159f / 180;
            lv_obj_t *n = th_text(scr, th->body, th->text, nums[i]);
            lv_obj_align(n, LV_ALIGN_CENTER, (int)(sinf(a) * 156), (int)(-cosf(a) * 156));
        }
        ck.window = th_text(scr, th->small, th->dim, "");
        lv_obj_set_style_border_width(ck.window, 2, 0);
        lv_obj_set_style_border_color(ck.window, lv_color_hex(th->faint), 0);
        lv_obj_set_style_radius(ck.window, th->radius, 0);
        lv_obj_set_style_pad_hor(ck.window, 12, 0);
        lv_obj_set_style_pad_ver(ck.window, 4, 0);
        lv_obj_align(ck.window, LV_ALIGN_CENTER, 0, 78);
        const int width[3] = {10, 6, 3};
        const uint32_t color[3] = {th->text, th->text, th->accent};
        for (int i = 0; i < 3; i++) {
            ck.hands[i] = lv_line_create(scr);
            lv_obj_set_style_line_width(ck.hands[i], width[i], 0);
            lv_obj_set_style_line_rounded(ck.hands[i], true, 0);
            lv_obj_set_style_line_color(ck.hands[i], lv_color_hex(color[i]), 0);
        }
        dot(scr, SCR / 2, SCR / 2, 18, th->accent);
    } else { /* CLOCK_BIG: date in red, the time, rings for food/fun/love, two little complications */
        ck.date = th_text(scr, th->body, 0xFF4245, "");
        lv_obj_align(ck.date, LV_ALIGN_TOP_MID, 0, 62);
        ck.info = th_text(scr, th->digits, th->text, "");
        lv_obj_align(ck.info, LV_ALIGN_TOP_MID, 0, 100);
        ck.arcs[0] = ring_arc(scr, 108, 11, 0xFF375F, 330);
        ck.arcs[1] = ring_arc(scr, 80, 11, 0x30D158, 330);
        ck.arcs[2] = ring_arc(scr, 52, 11, 0x3CD3FE, 330);
        for (int i = 0; i < 2; i++) {
            lv_obj_t *c = th_box(scr, i ? 312 : 74, 292, 80, 80, LV_RADIUS_CIRCLE, th->panel);
            ck.comp[i] = th_text(c, th->small, i ? 0x30D158 : 0x0091FF, "");
            lv_obj_center(ck.comp[i]);
        }
    }
    clock_tick(NULL);
    ck.timer = lv_timer_create(clock_tick, 250, NULL);
}

static void clock_close(void) { lv_timer_delete(ck.timer); }

/* ================= timer: drag the arc to set minutes, it keeps running when you leave ================= */

static struct {
    int minutes;
    bool running, open;
    uint32_t end_tick, left_ms; /* left_ms when paused */
    lv_obj_t *arc, *time, *start;
    lv_timer_t *ui;
    /* the terminal face: a ring of 60 one-minute cells with a knob, block digits, two buttons */
    lv_obj_t *grid, *head, *sub, *go, *pause, *foot;
    th_cell_t tc[70 + 147 + 60 + 2]; /* "MM" while setting, "MM:SS" while counting, the ring, the knob */
} tm_ = {.minutes = 5};
#define TM_MM 0
#define TM_MMSS 70
#define TM_RING (70 + 147)
#define TM_KNOB (70 + 147 + 60)

static uint32_t timer_left(void)
{
    if (!tm_.running) return tm_.left_ms;
    int32_t left = (int32_t)(tm_.end_tick - lv_tick_get());
    return left > 0 ? (uint32_t)left : 0;
}

static void timer_ui_term(void)
{
    uint32_t left = timer_left();
    bool fresh = !tm_.running && left == (uint32_t)tm_.minutes * 60000;
    char t[32];
    lv_label_set_text(tm_.head, th_label(fresh ? "пип-01 // таймер" : tm_.running ? "таймер // идёт" : "таймер // пауза"));
    if (fresh) {
        snprintf(t, sizeof t, "%02d", tm_.minutes);
        th_blocks_hide(tm_.grid, tm_.tc + TM_MMSS, 147);
        th_blocks_show(tm_.grid, tm_.tc + TM_MM, t, th->text);
        lv_label_set_text(tm_.sub, th_label("минут"));
    } else {
        snprintf(t, sizeof t, "%02u:%02u", (unsigned)(left / 60000), (unsigned)(left / 1000 % 60));
        th_blocks_hide(tm_.grid, tm_.tc + TM_MM, 70);
        th_blocks_show(tm_.grid, tm_.tc + TM_MMSS, t, tm_.running ? th->text : th->dim);
        snprintf(t, sizeof t, "из %02d:00", tm_.minutes);
        lv_label_set_text(tm_.sub, th_label(t));
    }
    /* a cell a minute: set, or still to go; the running minute blinks */
    int lit = fresh ? tm_.minutes : (int)((left + 59999) / 60000);
    bool blink = tm_.running && lv_tick_get() / 500 % 2;
    for (int i = 0; i < 60; i++)
        th_cell_set(tm_.grid, &tm_.tc[TM_RING + i], i < lit - 1 || (i == lit - 1 && !blink) ? th->text : i == lit - 1 ? th->dim : th->off);
    th_cell_t *k = &tm_.tc[TM_KNOB];
    float a = (tm_.minutes - 0.5f) * 6.2831853f / 60 - 1.5707963f;
    int kx = (int)lroundf(SCR / 2 + cosf(a) * 196), ky = (int)lroundf(SCR / 2 + sinf(a) * 196);
    if (k[0].x != kx - 11 || k[0].y != ky - 11 || (k[0].color == TH_NONE) == fresh) {
        k[0] = (th_cell_t){(int16_t)(kx - 11), (int16_t)(ky - 11), 22, 22, fresh ? th->text : TH_NONE};
        k[1] = (th_cell_t){(int16_t)(kx - 4), (int16_t)(ky - 4), 8, 8, fresh ? th->bg : TH_NONE};
        lv_obj_invalidate(tm_.grid);
    }
    lv_obj_set_flag(tm_.go, LV_OBJ_FLAG_HIDDEN, tm_.running);
    lv_obj_set_flag(tm_.pause, LV_OBJ_FLAG_HIDDEN, !tm_.running);
    lv_label_set_text(tm_.foot, th_label(fresh ? TH_DOWN " назад" : TH_DOWN " сброс"));
}

static void timer_ui(lv_timer_t *t)
{
    (void)t;
    if (!tm_.open) return;
    if (TH_TERMINAL) { timer_ui_term(); return; }
    uint32_t left = timer_left();
    bool fresh = !tm_.running && left == (uint32_t)tm_.minutes * 60000;
    lv_label_set_text_fmt(tm_.time, "%02u:%02u", (unsigned)(left / 60000), (unsigned)((left / 1000) % 60));
    lv_label_set_text(lv_obj_get_child(tm_.start, 0), tm_.running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (!fresh) lv_arc_set_value(tm_.arc, (int32_t)((left + 59999) / 60000));
    lv_obj_set_state(tm_.arc, LV_STATE_DISABLED, !fresh); /* the knob only sets a fresh timer */
}

static void timer_watch(lv_timer_t *t) /* runs always: rings even when the app is closed */
{
    (void)t;
    if (tm_.running && timer_left() == 0) {
        tm_.running = false;
        tm_.left_ms = (uint32_t)tm_.minutes * 60000;
        shell_home();
        pip_react(PIP_SURPRISED, 4, "time's up");
    }
}

static void timer_set(lv_event_t *e)
{
    (void)e;
    tm_.minutes = lv_arc_get_value(tm_.arc);
    tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    timer_ui(NULL);
}

static void timer_start(lv_event_t *e)
{
    (void)e;
    if (tm_.running) tm_.left_ms = timer_left(), tm_.running = false;
    else tm_.end_tick = lv_tick_get() + tm_.left_ms, tm_.running = true;
    timer_ui(NULL);
}

static void timer_reset(lv_event_t *e)
{
    (void)e;
    tm_.running = false;
    tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    if (tm_.arc) lv_arc_set_value(tm_.arc, tm_.minutes);
    timer_ui(NULL);
}

/* top: start and pause; bottom: reset a used timer, or leave a fresh one */
static bool timer_button(int id)
{
    bool fresh = !tm_.running && timer_left() == (uint32_t)tm_.minutes * 60000;
    if (id == PIP_BTN_PWR) timer_start(NULL);
    else if (!fresh) timer_reset(NULL);
    else return false;
    return true;
}

static void timer_drag(lv_event_t *e) /* the terminal ring: drag round it to set the minutes */
{
    (void)e;
    if (tm_.running || timer_left() != (uint32_t)tm_.minutes * 60000) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    float dx = p.x - SCR / 2, dy = p.y - SCR / 2;
    if (hypotf(dx, dy) < 140) return; /* the middle is for the button */
    float a = atan2f(dx, -dy) * 57.2958f;
    int m = (int)ceilf((a < 0 ? a + 360 : a) / 6);
    tm_.minutes = m < 1 ? 1 : m > 60 ? 60 : m;
    tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    timer_ui(NULL);
}

static void timer_open_term(lv_obj_t *scr)
{
    tm_.head = th_header(scr, "", 92);
    th_blocks(tm_.tc + TM_MM, 174, 163, "00", 11, 9);
    th_blocks(tm_.tc + TM_MMSS, 97, 163, "00:00", 11, 9);
    th_ring(tm_.tc + TM_RING, 60, 196, 9);
    tm_.tc[TM_KNOB] = tm_.tc[TM_KNOB + 1] = (th_cell_t){0, 0, 1, 1, TH_NONE};
    tm_.grid = th_cells(scr, tm_.tc, TM_KNOB + 2);
    tm_.sub = th_text_at(scr, th->tiny, th->dim, "", -1, 251);
    lv_obj_set_style_text_letter_space(tm_.sub, 2, 0);
    tm_.go = th_button_at(scr, 163, 286, 140, TH_UP " старт", true, timer_start);
    tm_.pause = th_button_at(scr, 163, 286, 140, TH_UP " пауза", false, timer_start);
    tm_.foot = th_footer(scr, "", 347);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, timer_drag, LV_EVENT_PRESSING, NULL);
}

static void timer_open(lv_obj_t *scr)
{
    if (!tm_.running && !tm_.left_ms) tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    tm_.open = true;
    if (TH_TERMINAL) {
        timer_open_term(scr);
        timer_ui(NULL);
        tm_.ui = lv_timer_create(timer_ui, 100, NULL);
        return;
    }
    tm_.arc = lv_arc_create(scr);
    lv_obj_set_size(tm_.arc, 400, 400);
    lv_obj_center(tm_.arc);
    lv_arc_set_range(tm_.arc, 1, 60);
    lv_arc_set_value(tm_.arc, tm_.minutes);
    lv_arc_set_bg_angles(tm_.arc, 135, 45);
    lv_obj_set_style_arc_width(tm_.arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(tm_.arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(tm_.arc, th->radius != 0, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(tm_.arc, th->radius != 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(tm_.arc, lv_color_hex(th->faint), LV_PART_MAIN);
    lv_obj_set_style_arc_color(tm_.arc, lv_color_hex(th->accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(tm_.arc, lv_color_hex(th->text), LV_PART_KNOB);
    lv_obj_set_style_radius(tm_.arc, th->radius, LV_PART_KNOB);
    lv_obj_set_style_pad_all(tm_.arc, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(tm_.arc, timer_set, LV_EVENT_VALUE_CHANGED, NULL);
    tm_.time = th_text(scr, th->digits, th->text, "");
    lv_obj_align(tm_.time, LV_ALIGN_CENTER, 0, -30);
    tm_.start = th_button(scr, LV_SYMBOL_PLAY, true, timer_start);
    lv_obj_set_size(tm_.start, 84, 64);
    lv_obj_align(tm_.start, LV_ALIGN_CENTER, -50, 92);
    lv_obj_t *reset = th_button(scr, LV_SYMBOL_REFRESH, false, timer_reset);
    lv_obj_set_size(reset, 84, 64);
    lv_obj_align(reset, LV_ALIGN_CENTER, 50, 92);
    timer_ui(NULL);
    tm_.ui = lv_timer_create(timer_ui, 200, NULL);
}

static void timer_close(void)
{
    lv_timer_delete(tm_.ui);
    tm_.arc = NULL;
    tm_.open = false;
}

/* ================= torch: the whole AMOLED as a lamp; tap for colour, drag up/down for brightness ================= */

static struct {
    lv_obj_t *scr, *hint;
    int color;
    float level;
    int32_t drag_y;
} tc = {.level = 1};

static void torch_paint(void)
{
    static const uint32_t colors[] = {0xFFFFFF, 0xFFD9A0, 0xFF2A1A};
    uint32_t c = colors[tc.color];
    lv_obj_set_style_bg_image_src(tc.scr, NULL, 0);
    lv_obj_set_style_bg_color(tc.scr, lv_color_make((uint8_t)((c >> 16 & 255) * tc.level), (uint8_t)((c >> 8 & 255) * tc.level),
                                                    (uint8_t)((c & 255) * tc.level)), 0);
}

static void torch_input(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) tc.drag_y = p.y;
    else if (code == LV_EVENT_PRESSING) {
        tc.level = fmaxf(0.15f, fminf(1, tc.level - (p.y - tc.drag_y) / 300.0f));
        tc.drag_y = p.y;
        torch_paint();
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        tc.color = (tc.color + 1) % 3;
        torch_paint();
    }
}

static void torch_open(lv_obj_t *scr)
{
    tc.scr = scr;
    torch_paint();
    tc.hint = th_text(scr, th->small, 0x777777, "тап — цвет\nвверх/вниз — яркость");
    lv_obj_center(tc.hint);
    lv_obj_fade_out(tc.hint, 600, 1800);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, torch_input, LV_EVENT_ALL, NULL);
}

/* ================= stopwatch: top button starts and marks laps, bottom resets; tap pauses ================= */

#define MAX_LAPS 99
static struct {
    bool running;
    uint32_t start, acc; /* ms: when it last started, and what came before */
    uint32_t laps[MAX_LAPS];
    int n;
    lv_obj_t *time, *cs, *list;
    lv_timer_t *ui;
    lv_obj_t *grid, *lap_rows[3], *foot; /* terminal */
    th_cell_t tc[147];
    int drawn;
} sw;

static uint32_t sw_elapsed(void) { return sw.acc + (sw.running ? lv_tick_elaps(sw.start) : 0); }

static void sw_draw_term(void)
{
    uint32_t e = sw_elapsed();
    char t[32];
    snprintf(t, sizeof t, "%02u:%02u", (unsigned)(e / 60000 % 100), (unsigned)(e / 1000 % 60));
    th_blocks_show(sw.grid, sw.tc, t, sw.running || !e ? th->text : th->dim);
    lv_label_set_text_fmt(sw.cs, ".%02u", (unsigned)(e / 10 % 100));
    int state = sw.n * 4 + sw.running * 2 + (e > 0);
    if (state == sw.drawn) return;
    sw.drawn = state;
    for (int k = 0; k < 3; k++) { /* the last three laps, newest first */
        int i = sw.n - 1 - k;
        if (i < 0) { lv_label_set_text(sw.lap_rows[k], ""); continue; }
        uint32_t lap = sw.laps[i] - (i ? sw.laps[i - 1] : 0);
        char key[16];
        snprintf(key, sizeof key, "кр %02d", i + 1);
        snprintf(t, sizeof t, "%02u:%02u.%02u", (unsigned)(lap / 60000), (unsigned)(lap / 1000 % 60), (unsigned)(lap / 10 % 100));
        th_leader(sw.lap_rows[k], key, t, 234);
    }
    lv_label_set_text(sw.foot, th_label(sw.running ? TH_UP " круг   " TH_DOWN " сброс" : e ? TH_UP " дальше   " TH_DOWN " сброс"
                                                                                       : TH_UP " старт   " TH_DOWN " назад"));
}

static void sw_draw(lv_timer_t *t)
{
    (void)t;
    if (!sw.time) return;
    if (TH_TERMINAL) { sw_draw_term(); return; }
    uint32_t e = sw_elapsed();
    lv_label_set_text_fmt(sw.time, "%02u:%02u", (unsigned)(e / 60000 % 100), (unsigned)(e / 1000 % 60));
    lv_label_set_text_fmt(sw.cs, ".%02u", (unsigned)(e / 10 % 100));
    char buf[160] = "", line[48];
    for (int i = sw.n - 1; i >= 0 && i >= sw.n - 3; i--) { /* the last three laps, newest first */
        uint32_t lap = sw.laps[i] - (i ? sw.laps[i - 1] : 0);
        snprintf(line, sizeof line, "%sкруг %d   %02u:%02u.%02u", buf[0] ? "\n" : "", i + 1, (unsigned)(lap / 60000),
                 (unsigned)(lap / 1000 % 60), (unsigned)(lap / 10 % 100));
        strncat(buf, line, sizeof buf - strlen(buf) - 1);
    }
    lv_label_set_text(sw.list, th_label(buf));
    lv_obj_set_style_text_color(sw.time, lv_color_hex(sw.running || !e ? th->text : th->dim), 0);
}

static bool sw_button(int id)
{
    if (id == PIP_BTN_PWR) { /* start, then a lap mark every press */
        if (!sw.running) sw.start = lv_tick_get(), sw.running = true;
        else if (sw.n < MAX_LAPS) sw.laps[sw.n++] = sw_elapsed();
    } else { /* reset, or leave once there is nothing to reset */
        if (!sw.running && !sw_elapsed()) return false;
        sw.running = false, sw.acc = 0, sw.n = 0;
    }
    sw_draw(NULL);
    return true;
}

static void sw_tap(lv_event_t *e) /* pause and go on */
{
    (void)e;
    if (sw.running) sw.acc = sw_elapsed(), sw.running = false;
    else if (sw.acc) sw.start = lv_tick_get(), sw.running = true;
    sw_draw(NULL);
}

static void sw_open(lv_obj_t *scr)
{
    if (TH_TERMINAL) {
        th_header(scr, "пип-01 // секундомер", 40);
        th_blocks(sw.tc, 97, 97, "00:00", 11, 9);
        sw.grid = th_cells(scr, sw.tc, 147);
        sw.time = sw.grid; /* "open" */
        sw.cs = th_text_at(scr, th->body, th->dim, "", -1, 186);
        const uint32_t ink[3] = {th->text, th->dim, th->mute};
        for (int k = 0; k < 3; k++) sw.lap_rows[k] = th_text_at(scr, th->tiny, ink[k], "", 116, 237 + k * 26);
        th_text_at(scr, th->tiny, th->mute, "тап: пауза", -1, 341);
        sw.foot = th_footer(scr, "", 367);
        sw.drawn = -1;
        lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(scr, sw_tap, LV_EVENT_SHORT_CLICKED, NULL);
        sw_draw(NULL);
        sw.ui = lv_timer_create(sw_draw, 33, NULL);
        return;
    }
    sw.time = th_text(scr, th->digits, th->text, "");
    lv_obj_align(sw.time, LV_ALIGN_CENTER, 0, -60);
    sw.cs = th_text(scr, th->body, th->dim, "");
    lv_obj_align(sw.cs, LV_ALIGN_CENTER, 0, 4);
    sw.list = th_text(scr, th->small, th->text, "");
    lv_obj_align(sw.list, LV_ALIGN_CENTER, 0, 74);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, sw_tap, LV_EVENT_SHORT_CLICKED, NULL);
    sw_draw(NULL);
    sw.ui = lv_timer_create(sw_draw, 33, NULL);
}

static void sw_close(void)
{
    lv_timer_delete(sw.ui);
    sw.time = NULL;
}

/* ================= noise: a sound level meter for the studio, the colour says how quiet it is ================= */

#define NZ_LO 20  /* the dial's range, dB */
#define NZ_HI 100
#define NZ_BARS 40 /* the last 20 s, a bar per half second */

static const struct { float below; uint32_t color; const char *word; } nz_bands[] = {
    {30, 0x34C759, "тишина"}, {40, 0x9BE15D, "тихо"}, {55, 0xFFD60A, "разговор"}, {70, 0xFF9F0A, "шумно"}, {999, 0xFF453A, "громко"},
};

static struct {
    lv_obj_t *arc, *num, *unit, *word, *span, *bars;
    th_cell_t cells[NZ_BARS];
    lv_timer_t *timer;
    float slow, peak, lo, hi, hist[NZ_BARS];
    int n, ticks;
} nz;

static int nz_band(float db)
{
    int i = 0;
    while (db >= nz_bands[i].below) i++;
    return i;
}

static void nz_reset(void) { nz.lo = 999, nz.hi = 0, nz.n = 0, nz.slow = 0; }

static void nz_update(lv_timer_t *t)
{
    (void)t;
    if (!mic_on()) return;
    float db = mic_db();
    nz.slow = nz.slow ? nz.slow + (db - nz.slow) * 0.1f : db; /* about a second: the number holds still enough to read */
    nz.peak = fmaxf(nz.peak, db);
    int b = nz_band(nz.slow);
    uint32_t c = nz_bands[b].color;
    lv_arc_set_value(nz.arc, (int32_t)fminf(NZ_HI, fmaxf(NZ_LO, db)));
    lv_obj_set_style_arc_color(nz.arc, lv_color_hex(nz_bands[nz_band(db)].color), LV_PART_INDICATOR);
    lv_label_set_text_fmt(nz.num, "%d", (int)lroundf(nz.slow));
    lv_obj_set_style_text_color(nz.num, lv_color_hex(c), 0);
    lv_label_set_text(nz.word, th_label(nz_bands[b].word));
    lv_obj_set_style_text_color(nz.word, lv_color_hex(c), 0);
    if (++nz.ticks % 5) return;
    /* every half second: a bar of the loudest moment in it, and the session's quietest and loudest */
    nz.lo = fminf(nz.lo, nz.slow), nz.hi = fmaxf(nz.hi, nz.peak);
    memmove(nz.hist, nz.hist + 1, sizeof nz.hist - sizeof nz.hist[0]);
    nz.hist[NZ_BARS - 1] = nz.peak, nz.peak = 0, nz.n++;
    for (int i = 0; i < NZ_BARS; i++) {
        float v = nz.hist[i];
        int h = v > 0 ? 3 + (int)(fminf(1, fmaxf(0, (v - NZ_LO) / (NZ_HI - NZ_LO))) * 44) : 3;
        nz.cells[i].y = (int16_t)(352 - h), nz.cells[i].h = (int16_t)h;
        nz.cells[i].color = v > 0 ? nz_bands[nz_band(v)].color : th_off();
    }
    lv_obj_invalidate(nz.bars);
    char s[48];
    snprintf(s, sizeof s, "мин %d   макс %d", (int)lroundf(nz.lo), (int)lroundf(nz.hi));
    lv_label_set_text(nz.span, th_label(s));
}

static void nz_tap(lv_event_t *e) { (void)e; nz_reset(), memset(nz.hist, 0, sizeof nz.hist), nz.ticks = 4, nz_update(NULL); }

static void nz_close(void)
{
    if (nz.timer) lv_timer_delete(nz.timer), nz.timer = NULL;
}

static void nz_open(lv_obj_t *scr)
{
    nz_reset();
    memset(nz.hist, 0, sizeof nz.hist);
    nz.peak = 0, nz.ticks = 0;
    /* the dial: 20..100 dB over three quarters of a turn, open at the bottom */
    lv_obj_t *a = nz.arc = lv_arc_create(scr);
    lv_obj_set_size(a, 420, 420);
    lv_obj_center(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);
    lv_arc_set_range(a, NZ_LO, NZ_HI);
    lv_arc_set_value(a, NZ_LO);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(a, 10, 0);
    lv_obj_set_style_arc_color(a, lv_color_hex(th->faint), 0);
    lv_obj_set_style_arc_width(a, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, th->radius > 0, 0);
    lv_obj_set_style_arc_rounded(a, th->radius > 0, LV_PART_INDICATOR);
    nz.num = th_text(scr, th->digits, th->text, mic_on() ? "" : "--");
    lv_obj_align(nz.num, LV_ALIGN_CENTER, 0, -50);
    nz.unit = th_text(scr, th->small, th->dim, "");
    lv_label_set_text(nz.unit, "дБ"); /* the unit keeps its case */
    lv_obj_align(nz.unit, LV_ALIGN_CENTER, 0, 14);
    nz.word = th_text(scr, th->body, th->dim, mic_on() ? "" : "нет микрофона");
    lv_obj_align(nz.word, LV_ALIGN_CENTER, 0, 52);
    for (int i = 0; i < NZ_BARS; i++) nz.cells[i] = (th_cell_t){(int16_t)(SCR / 2 - NZ_BARS * 6 / 2 + i * 6), 349, 4, 3, th_off()};
    nz.bars = th_cells(scr, nz.cells, NZ_BARS);
    nz.span = th_text(scr, th->tiny, th_mute(), "");
    lv_obj_align(nz.span, LV_ALIGN_TOP_MID, 0, 364);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE); /* a tap starts the min/max and the bars over */
    lv_obj_add_event_cb(scr, nz_tap, LV_EVENT_SHORT_CLICKED, NULL);
    nz.timer = lv_timer_create(nz_update, 100, NULL);
    nz_update(NULL);
}

/* ================= the list: the first one sits in the middle of the honeycomb ================= */

const app_t apps[] = {
    {.id = "notify", .name = "Уведомления", .icon = ICON_BELL, .color = 0xFF375F, .open = phone_notify_open, .close = phone_notify_close,
     .button = phone_notify_button, .vertical = true},
    {.id = "music", .name = "Музыка", .icon = ICON_MUSIC, .color = 0xFF375F, .open = phone_music_open, .close = phone_music_close,
     .button = phone_music_button, .vertical = true},
    {.id = "remote", .name = "Пульт", .icon = ICON_REMOTE, .color = 0x5E5CE6, .open = phone_remote_open, .close = phone_remote_close,
     .button = phone_remote_button,
     .button_up = phone_remote_button_up, .imu = phone_remote_imu, .gyro = phone_remote_gyro, .awake = true},
    {.id = "clock", .name = "Часы", .icon = ICON_CLOCK, .color = 0x0091FF, .open = clock_open, .close = clock_close},
    {.id = "timer", .name = "Таймер", .icon = ICON_TIMER, .color = 0xFF9230, .open = timer_open, .close = timer_close,
     .button = timer_button, .awake = true},
    {.id = "stopwatch", .name = "Секундомер", .icon = ICON_STOPWATCH, .color = 0xFF4245, .open = sw_open, .close = sw_close,
     .button = sw_button, .awake = true},
    {.id = "torch", .name = "Фонарик", .icon = ICON_TORCH, .color = 0xFFD600, .open = torch_open, .awake = true},
    {.id = "noise", .name = "Шумомер", .icon = ICON_NOISE, .color = 0x34C759, .open = nz_open, .close = nz_close, .awake = true},
    {.id = "camera", .name = "Камера", .icon = ICON_CAMERA, .color = 0x30D158, .open = phone_camera_open, .close = phone_camera_close,
     .button = phone_camera_button, .awake = true},
    {.id = "bt", .name = "Bluetooth", .icon = ICON_BT, .color = 0x0A84FF, .open = phone_bt_open, .close = phone_bt_close},
};
const int app_total = sizeof apps / sizeof apps[0];
const int app_count = app_total; /* all of them are in the menu */

void apps_init(void) { lv_timer_create(timer_watch, 250, NULL); }
