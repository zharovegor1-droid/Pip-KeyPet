/* The assistant on Pip's face (ask.h): push to talk, the answer as text. */
#include "ask.h"
#include "pip.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR 466
#define ANSWER_MAX 2048   /* on the heap: that big goes to PSRAM on the board */
#define TAP_MS 350        /* let go sooner: it was a tap (a blink), not a question */
#define LISTEN_MAX_MS 30000
#define THINK_MAX_MS 45000
#define FAIL_MS 3000

static struct {
    int state;
    bool app, down, complete;
    volatile bool streaming;
    uint32_t down_at, since; /* lv_tick: the key went down; the state began */
    char heard[160];
    char *answer, *shown; /* as it came; what the fonts can draw of it */
    int len;
    char why[64];
    void (*cb)(int what);
    lv_obj_t *strip, *strip_l, *strip_h; /* over his face: "слушаю" and what was heard */
    lv_obj_t *o, *box, *text, *hint;    /* the answer */
    lv_timer_t *t;
} a;

static void send(int what)
{
    if (a.cb) a.cb(what);
}

void ask_on_host(void (*cb)(int)) { a.cb = cb; }
bool ask_streaming(void) { return a.streaming; }
bool ask_app_on(void) { return a.app; }
int ask_state(void) { return a.state; }
const char *ask_state_name(void)
{
    static const char *const names[] = {"idle", "listen", "think", "answer", "fail"};
    return names[a.state];
}
const char *ask_where(void) { return a.state == ASK_IDLE ? NULL : "ask"; }

/* copy UTF-8 without cutting a character in half */
static void copy(char *d, size_t n, const char *s)
{
    size_t k = 0;
    while (s && s[k] && k + 1 < n) k++;
    while (k > 0 && s[k] && ((unsigned char)s[k] & 0xC0) == 0x80) k--;
    memcpy(d, s ? s : "", k);
    d[k] = 0;
}

/* ---------- the strip over his face while he listens and thinks ---------- */
static void strip_hide(void)
{
    if (a.strip) lv_obj_delete(a.strip), a.strip = a.strip_l = a.strip_h = NULL;
}

static void strip_show(const char *what)
{
    if (!a.strip) {
        a.strip = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(a.strip);
        lv_obj_set_size(a.strip, 320, 90);
        lv_obj_align(a.strip, LV_ALIGN_TOP_MID, 0, 372); /* under his eyes, whatever they do */
        lv_obj_remove_flag(a.strip, LV_OBJ_FLAG_CLICKABLE);
        a.strip_l = lv_label_create(a.strip);
        lv_obj_set_style_text_font(a.strip_l, th->small, 0);
        lv_obj_set_style_text_color(a.strip_l, lv_color_hex(th->accent), 0);
        lv_obj_align(a.strip_l, LV_ALIGN_TOP_MID, 0, 0);
        a.strip_h = lv_label_create(a.strip);
        lv_obj_set_width(a.strip_h, 300);
        lv_label_set_long_mode(a.strip_h, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(a.strip_h, 2 * lv_font_get_line_height(th->tiny));
        lv_obj_set_style_text_font(a.strip_h, th->tiny, 0);
        lv_obj_set_style_text_color(a.strip_h, lv_color_hex(th->dim), 0);
        lv_obj_set_style_text_align(a.strip_h, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(a.strip_h, LV_ALIGN_TOP_MID, 0, 30);
    }
    lv_label_set_text(a.strip_l, what);
    lv_label_set_text(a.strip_h, a.heard);
}

/* ---------- the answer: a page of text that scrolls under the finger ---------- */
static void page_hide(void)
{
    if (a.o) lv_obj_delete(a.o), a.o = a.box = a.text = a.hint = NULL;
}

static void page_text(void)
{
    if (!a.text) return;
    if (!a.shown) a.shown = malloc(ANSWER_MAX);
    if (!a.shown) return;
    strcpy(a.shown, a.answer ? a.answer : "");
    th_drawable(a.shown); /* the whole of it each time: a piece's edge spaces are not the text's edges */
    lv_label_set_text(a.text, a.shown);
    lv_label_set_text(a.hint, a.state == ASK_FAIL ? "любая кнопка: назад"
                              : a.complete ? (TH_TERMINAL ? TH_UP " вслух   " TH_DOWN " назад" : "верх: вслух · низ: назад")
                                           : "пишет…");
}

static void page_show(const char *title, uint32_t color)
{
    page_hide();
    lv_obj_t *o = a.o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCR, SCR);
    th_screen(o);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(o); /* the question, one line */
    lv_obj_set_width(t, 300);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(t, lv_font_get_line_height(th->tiny));
    lv_obj_set_style_text_font(t, th->tiny, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(th->dim), 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 52);
    /* the round glass: 320 px wide fits between y 90 and 376 with a margin */
    a.box = lv_obj_create(o);
    lv_obj_remove_style_all(a.box);
    lv_obj_set_size(a.box, 320, 282);
    lv_obj_align(a.box, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_scroll_dir(a.box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(a.box, LV_SCROLLBAR_MODE_OFF);
    a.text = lv_label_create(a.box);
    lv_obj_set_width(a.text, 320);
    if (a.state == ASK_FAIL) lv_obj_set_style_text_align(a.text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(a.text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(a.text, th->small, 0);
    lv_obj_set_style_text_color(a.text, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(a.text, 4, 0);
    a.hint = lv_label_create(o);
    lv_obj_set_style_text_font(a.hint, th->tiny, 0);
    lv_obj_set_style_text_color(a.hint, lv_color_hex(th->dim), 0);
    lv_obj_align(a.hint, LV_ALIGN_TOP_MID, 0, 386);
    page_text();
}

/* ---------- the states ---------- */
static void tick(lv_timer_t *t);

static void go(int state)
{
    a.state = state;
    a.since = lv_tick_get();
    if (!a.t) a.t = lv_timer_create(tick, 100, NULL);
    if (state != ASK_LISTEN) a.streaming = false;
    if (state == ASK_IDLE || state == ASK_ANSWER || state == ASK_FAIL) strip_hide();
    if (state == ASK_IDLE) {
        page_hide();
        pip_info_t in;
        pip_get_info(&in);
        if (!strcmp(in.reaction, "listen") || !strcmp(in.reaction, "think")) pip_react(PIP_NEUTRAL, 0.01f, ""); /* ours: over */
    }
}

static void fail(const char *why)
{
    copy(a.why, sizeof a.why, why);
    if (a.state == ASK_LISTEN) send(ASK_CANCEL);
    go(ASK_FAIL);
    if (!a.answer) a.answer = malloc(ANSWER_MAX);
    if (a.answer) copy(a.answer, ANSWER_MAX, a.why), a.len = (int)strlen(a.answer);
    page_show(a.heard[0] ? a.heard : "ассистент", th->text);
    pip_react(PIP_PUZZLED, 2.5f, "no answer");
}

static void think(void)
{
    a.streaming = false;
    send(ASK_STOP);
    go(ASK_THINK);
    strip_show("думаю…");
    pip_react(PIP_SKEPTICAL, THINK_MAX_MS / 1000.0f, "think");
}

static void tick(lv_timer_t *t)
{
    (void)t;
    uint32_t in = lv_tick_elaps(a.since);
    if (a.state == ASK_LISTEN && a.down && lv_tick_elaps(a.down_at) >= TAP_MS && !a.strip) strip_show("слушаю…");
    if (a.state == ASK_LISTEN && in > LISTEN_MAX_MS) a.down = false, think(); /* he can only hold his breath so long */
    else if (a.state == ASK_THINK && in > THINK_MAX_MS) fail("Ответ не пришёл. Попробуй ещё раз.");
    else if (a.state == ASK_FAIL && in > FAIL_MS) go(ASK_IDLE);
    if (a.state == ASK_LISTEN) pip_react(PIP_CURIOUS, 1, "listen"); /* held for as long as he listens */
}

bool ask_press(void)
{
    if (a.state != ASK_IDLE) return false;
    a.down = true, a.down_at = lv_tick_get(), a.heard[0] = 0, a.complete = false, a.len = 0;
    if (a.answer) a.answer[0] = 0;
    if (!a.app) return true; /* decided when it is let go: a tap blinks, a hold says the app is missing */
    go(ASK_LISTEN);
    send(ASK_START);    /* first: the host gets ready for the voice */
    a.streaming = true; /* from the first instant: the first word is often said with the press */
    return true;
}

void ask_release(void)
{
    if (!a.down) return;
    a.down = false;
    bool tap = lv_tick_elaps(a.down_at) < TAP_MS;
    if (a.state == ASK_LISTEN) {
        if (tap) { /* a tap: no question, just the blink it always was */
            a.streaming = false;
            send(ASK_CANCEL);
            go(ASK_IDLE);
            pip_button(PIP_BTN_BOOT);
        } else think();
    } else if (a.state == ASK_IDLE) {
        if (tap) pip_button(PIP_BTN_BOOT);
        else fail("Нет связи с iPhone. Открой Pip Bridge.");
    }
}

bool ask_button(int id)
{
    switch (a.state) {
    case ASK_ANSWER:
        if (id == PIP_BTN_PWR && a.complete) send(ASK_SAY), pip_react(PIP_HAPPY, 1.5f, "reads");
        else if (id == PIP_BTN_BOOT) go(ASK_IDLE);
        return true;
    case ASK_FAIL: go(ASK_IDLE); return true;
    case ASK_THINK:
        if (id == PIP_BTN_BOOT) send(ASK_CANCEL), go(ASK_IDLE); /* never mind */
        return true;
    case ASK_LISTEN: return true; /* the key is held: the other one does nothing */
    }
    return false;
}

/* ---------- from the phone ---------- */
void ask_app(bool on)
{
    a.app = on;
    if (!on && (a.state == ASK_LISTEN || a.state == ASK_THINK)) fail("iPhone отключился.");
}

void ask_heard(const char *text)
{
    copy(a.heard, sizeof a.heard, text);
    th_drawable(a.heard);
    if (a.strip) lv_label_set_text(a.strip_h, a.heard);
}

void ask_answer(const char *piece)
{
    if (a.state != ASK_THINK && a.state != ASK_ANSWER) return; /* cancelled: whatever comes late is dropped */
    if (!a.answer && !(a.answer = malloc(ANSWER_MAX))) return;
    if (a.state == ASK_THINK) {
        a.len = 0, a.answer[0] = 0, a.complete = false;
        go(ASK_ANSWER);
        page_show(a.heard[0] ? a.heard : "ответ", th->text);
        pip_react(PIP_HAPPY, 1.5f, "answer");
    }
    const char *p = piece;
    for (; *p && a.len < ANSWER_MAX - 1; p++) a.answer[a.len++] = *p == '\t' ? '\n' : *p;
    if (((unsigned char)*p & 0xC0) == 0x80) { /* full in the middle of a letter: that letter goes */
        while (a.len > 0 && ((unsigned char)a.answer[a.len - 1] & 0xC0) == 0x80) a.len--;
        if (a.len > 0) a.len--;
    }
    a.answer[a.len] = 0;
    page_text();
}

void ask_done(void)
{
    if (a.state == ASK_THINK) ask_answer("…"); /* an empty answer still closes the question */
    if (a.state != ASK_ANSWER) return;
    a.complete = true;
    page_text();
}

void ask_fail(const char *why)
{
    if (a.state == ASK_LISTEN || a.state == ASK_THINK) fail(why && *why ? why : "Что-то пошло не так.");
}
