/* The assistant on Pip's face (ask.h): push to talk, the answers as a chat, the chats kept. */
#include "ask.h"
#include "pip.h"
#include "speech.h"
#include "theme.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR 466
#define ANSWER_MAX 2048    /* on the heap: that big goes to PSRAM on the board */
#define TAP_MS 350         /* let go sooner: a tap (a blink on his face, back in a chat), not a question */
#define LISTEN_MAX_MS 30000
#define THINK_MAX_MS 45000
#define CONTEXT_MAX 2500   /* what of a long chat goes to Claude */
#define MAGIC 0x31484350u  /* "PCH1" */

/* the chat page: one column that scrolls, inside the round glass */
#define BOX_X 83
#define BOX_Y 76
#define BOX_W 300
#define BOX_H 308
#define BUBBLE_W 250
#define PAD_X 12
#define PAD_Y 8
#define GAP 8

/* A chat as kept: records "Q<text>\0" (asked) and "A<text>\0" (answered), oldest first. Exactly one slot. */
typedef struct {
    uint32_t magic, id, seq;
    uint16_t len, n;
    char t[ASK_SLOT_BYTES - 16];
} chat_t;

static struct {
    int state;
    bool app, down, in_chat; /* in_chat: the key went down in an open chat (a tap goes back) */
    volatile bool streaming;
    uint32_t down_at, since; /* lv_tick: the key went down; the state began */
    char heard[200];
    bool q_done, answering;  /* the question is in the chat; the answer is coming in */
    char *answer;
    int len;
    char fail[96];
    chat_t *work;            /* the open conversation */
    int slot;                /* where it is kept, -1: not yet */
    chat_t *chats[ASK_SLOTS];
    bool loaded;
    void (*cb)(int what);
    void (*save)(int slot, const void *data, int len);
    int (*load)(int slot, void *data, int max);
    lv_obj_t *strip, *strip_l, *strip_h; /* over his face: "слушаю" and what was heard */
    lv_obj_t *o, *box, *hint;            /* the chat page */
    lv_obj_t *list;                      /* the Ассистент app's screen, while open */
    lv_timer_t *t;
    bool spoke;                          /* speech_busy() last tick: the hint follows it */
} a = {.slot = -1};

static void tick(lv_timer_t *t);

static void send(int what)
{
    if (a.cb) a.cb(what);
}

void ask_on_host(void (*cb)(int)) { a.cb = cb; }
void ask_on_store(void (*save)(int, const void *, int), int (*load)(int, void *, int)) { a.save = save, a.load = load, a.loaded = false; }
bool ask_streaming(void) { return a.streaming; }
bool ask_app_on(void) { return a.app; }
int ask_state(void) { return a.state; }
const char *ask_state_name(void)
{
    static const char *const names[] = {"idle", "listen", "think", "chat"};
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

/* ---------- the chats ---------- */
static void chats_load(void)
{
    if (a.loaded) return;
    a.loaded = true;
    for (int i = 0; i < ASK_SLOTS; i++) {
        if (!a.chats[i] && !(a.chats[i] = malloc(sizeof(chat_t)))) continue;
        chat_t *c = a.chats[i];
        int got = a.load ? a.load(i, c, sizeof *c) : 0;
        if (got != (int)sizeof *c || c->magic != MAGIC || c->len > sizeof c->t || (c->len && c->t[c->len - 1])) memset(c, 0, sizeof *c);
    }
}

int ask_chat_count(void)
{
    chats_load();
    int n = 0;
    for (int i = 0; i < ASK_SLOTS; i++) n += a.chats[i] && a.chats[i]->magic == MAGIC && a.chats[i]->n;
    return n;
}

static void chat_push(chat_t *c, char who, const char *text)
{
    size_t k = strlen(text), room = sizeof c->t;
    if (k + 2 > room) k = room - 2;
    while (c->n && c->len + k + 2 > room) { /* full: the oldest go */
        size_t first = strlen(c->t) + 1;
        memmove(c->t, c->t + first, c->len - first);
        c->len -= (uint16_t)first, c->n--;
    }
    c->t[c->len] = who;
    char *d = c->t + c->len + 1;
    copy(d, k + 1, text);
    c->len += (uint16_t)(strlen(d) + 2), c->n++;
}

/* the i-th record: its kind and text; NULL past the end */
static const char *chat_rec(const chat_t *c, int i, char *who)
{
    const char *p = c->t;
    for (int k = 0; k < c->n; k++, p += strlen(p) + 1)
        if (k == i) return *who = p[0], p + 1;
    return NULL;
}

static void chat_keep(void) /* the open conversation into its slot, newest */
{
    chats_load();
    chat_t *w = a.work;
    if (!w || !w->n) return;
    uint32_t top = 0;
    int slot = a.slot, oldest = -1;
    for (int i = 0; i < ASK_SLOTS; i++) {
        chat_t *c = a.chats[i];
        if (!c) continue;
        if (c->magic == MAGIC && c->seq > top) top = c->seq;
        bool empty = c->magic != MAGIC || !c->n;
        if (slot < 0 && empty) slot = i;
        if (!empty && (oldest < 0 || c->seq < a.chats[oldest]->seq)) oldest = i;
    }
    if (slot < 0) slot = oldest;
    if (slot < 0 || !a.chats[slot]) return;
    w->magic = MAGIC, w->seq = top + 1;
    if (!w->id) w->id = lv_rand(1, 0x7FFFFFFF);
    memcpy(a.chats[slot], w, sizeof *w);
    a.slot = slot;
    if (a.save) a.save(slot, w, sizeof *w);
}

static chat_t *work(void)
{
    if (!a.work && (a.work = malloc(sizeof(chat_t)))) memset(a.work, 0, sizeof *a.work);
    return a.work;
}

static void chat_new(void)
{
    if (work()) memset(a.work, 0, sizeof *a.work);
    a.slot = -1;
}

void ask_context(void (*fn)(char who, const char *text, void *arg), void *arg)
{
    chat_t *w = a.work;
    if (!w || !w->n) return;
    int from = w->n, bytes = 0; /* back from the end while it fits */
    char who = 0;
    for (int i = w->n - 1; i >= 0; i--) {
        const char *t = chat_rec(w, i, &who);
        if (bytes + (int)strlen(t) > CONTEXT_MAX && from < w->n) break;
        bytes += (int)strlen(t), from = i;
    }
    while (from < w->n && chat_rec(w, from, &who) && who != 'Q') from++; /* Claude wants a question first */
    for (int i = from; i < w->n; i++) {
        const char *t = chat_rec(w, i, &who);
        fn(who, t, arg);
    }
}

const char *ask_say_text(void)
{
    if (a.answering && a.answer) return a.answer;
    const char *last = NULL;
    char who;
    for (int i = 0; a.work && i < a.work->n; i++) {
        const char *t = chat_rec(a.work, i, &who);
        if (who == 'A') last = t;
    }
    return last ? last : "";
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
        lv_obj_set_size(a.strip, 300, 90);
        lv_obj_align(a.strip, LV_ALIGN_TOP_MID, 0, 366); /* under his eyes, whatever they do */
        lv_obj_remove_flag(a.strip, LV_OBJ_FLAG_CLICKABLE);
        a.strip_l = lv_label_create(a.strip);
        lv_obj_set_style_text_font(a.strip_l, th->small, 0);
        lv_obj_set_style_text_color(a.strip_l, lv_color_hex(th->accent), 0);
        lv_obj_align(a.strip_l, LV_ALIGN_TOP_MID, 0, 0);
        a.strip_h = lv_label_create(a.strip);
        lv_obj_set_width(a.strip_h, 260); /* the circle narrows down here */
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

/* ---------- the chat page ---------- */
static int bubble(int y, bool mine, const char *text, uint32_t fg, uint32_t bg)
{
    size_t k = strlen(text);
    char *s = malloc(k + 1);
    if (!s) return y;
    for (size_t i = 0; i <= k; i++) s[i] = text[i] == '\t' ? '\n' : text[i];
    th_drawable(s);
    if (!*s) { free(s); return y; }
    lv_point_t sz;
    lv_text_get_size(&sz, s, th->small, 0, 4, BUBBLE_W - 2 * PAD_X, LV_TEXT_FLAG_NONE);
    int w = sz.x + 2 + 2 * PAD_X, h = sz.y + 2 * PAD_Y;
    lv_obj_t *b = lv_obj_create(a.box);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, mine ? BOX_W - w : 0, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, TH_TERMINAL ? 0 : 16, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE); /* the finger scrolls the column, not the bubble */
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_width(l, sz.x + 2);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(l, th->small, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_set_style_text_line_space(l, 4, 0);
    lv_label_set_text(l, s);
    lv_obj_set_pos(l, PAD_X, PAD_Y);
    free(s);
    return y + h + GAP;
}

static void page_hint(void)
{
    if (!a.hint) return;
    const char *t = a.state == ASK_LISTEN ? "отпусти — и я подумаю"
                    : a.state == ASK_THINK ? (TH_TERMINAL ? TH_DOWN " отменить" : "низ — отменить")
                    : speech_busy()        ? (TH_TERMINAL ? TH_UP " тише" : "верх — тише")
                    : a.answering          ? "пишет…"
                    : TH_TERMINAL          ? TH_UP " вслух  " TH_DOWN " назад/держи"
                                           : "держи низ — ещё";
    lv_label_set_text(a.hint, t);
}

static void page_build(void)
{
    if (!a.o) {
        lv_obj_t *o = a.o = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, SCR, SCR);
        th_screen(o);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        th_header(o, "пип-01 // чат", 40);
        a.box = lv_obj_create(o);
        lv_obj_remove_style_all(a.box);
        lv_obj_set_pos(a.box, BOX_X, BOX_Y);
        lv_obj_set_size(a.box, BOX_W, BOX_H);
        lv_obj_set_scroll_dir(a.box, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(a.box, LV_SCROLLBAR_MODE_OFF);
        a.hint = lv_label_create(o);
        lv_obj_set_style_text_font(a.hint, th->tiny, 0);
        lv_obj_set_style_text_color(a.hint, lv_color_hex(th->dim), 0);
        lv_obj_align(a.hint, LV_ALIGN_TOP_MID, 0, 392);
    }
    lv_obj_clean(a.box);
    int y = 4;
    char who;
    for (int i = 0; a.work && i < a.work->n; i++) {
        const char *t = chat_rec(a.work, i, &who);
        y = who == 'Q' ? bubble(y, true, t, th->bg, th->accent) : bubble(y, false, t, th->text, th->panel);
    }
    bool asking = a.state == ASK_LISTEN || a.state == ASK_THINK || a.fail[0];
    if (asking && !a.q_done && a.heard[0]) y = bubble(y, true, a.heard, th->bg, th->accent);
    if (a.answering && a.answer && a.len) y = bubble(y, false, a.answer, th->text, th->panel);
    if (a.state == ASK_LISTEN) y = bubble(y, false, "слушаю…", th->dim, th->panel);
    if (a.state == ASK_THINK) y = bubble(y, false, "думаю…", th->dim, th->panel);
    if (a.fail[0]) y = bubble(y, false, a.fail, th->dim, th->panel);
    if (y == 4) bubble(y, false, "спроси что-нибудь: держи нижнюю кнопку", th->dim, th->panel);
    lv_obj_update_layout(a.box);
    lv_obj_scroll_to_y(a.box, LV_COORD_MAX, LV_ANIM_OFF); /* the newest at the bottom, in view */
    page_hint();
}

static void page_hide(void)
{
    if (a.o) lv_obj_delete(a.o), a.o = a.box = a.hint = NULL;
}

/* ---------- the chat list (the Ассистент app) ---------- */
static void list_build(void);

static void list_tap(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    chats_load();
    if (a.state != ASK_IDLE || !a.chats[slot] || !work()) return;
    memcpy(a.work, a.chats[slot], sizeof *a.work);
    a.slot = slot;
    a.heard[0] = a.fail[0] = 0, a.q_done = a.answering = false;
    a.state = ASK_CHAT, a.since = lv_tick_get();
    if (!a.t) a.t = lv_timer_create(tick, 100, NULL);
    page_build();
}

static void list_build(void)
{
    lv_obj_t *scr = a.list;
    if (!scr) return;
    lv_obj_clean(scr);
    th_header(scr, "пип-01 // ассистент", 40);
    chats_load();
    int order[ASK_SLOTS], n = 0;
    for (int i = 0; i < ASK_SLOTS; i++)
        if (a.chats[i] && a.chats[i]->magic == MAGIC && a.chats[i]->n) order[n++] = i;
    for (int i = 1; i < n; i++) /* newest first */
        for (int j = i; j > 0 && a.chats[order[j]]->seq > a.chats[order[j - 1]]->seq; j--) {
            int t = order[j];
            order[j] = order[j - 1], order[j - 1] = t;
        }
    if (!n) {
        th_text_at(scr, th->body, th->dim, "пусто", -1, 190);
        th_text_at(scr, th->tiny, th->dim, "держи нижнюю кнопку на лице Пипа", -1, 236);
        return;
    }
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, BOX_X, 82);
    lv_obj_set_size(box, BOX_W, 300);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
    for (int k = 0; k < n; k++) {
        chat_t *c = a.chats[order[k]];
        lv_obj_t *p = th_box(box, 0, k * 74, BOX_W, 66, TH_TERMINAL ? 0 : 14, th->panel);
        lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(p, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(p, list_tap, LV_EVENT_CLICKED, (void *)(intptr_t)order[k]);
        char who, title[120];
        const char *q = chat_rec(c, 0, &who);
        copy(title, sizeof title, q ? q : "");
        for (char *s = title; *s; s++)
            if (*s == '\t' || *s == '\n') *s = ' ';
        th_drawable(title);
        lv_obj_t *l = lv_label_create(p);
        lv_obj_set_width(l, BOX_W - 28);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(l, lv_font_get_line_height(th->small));
        lv_obj_set_style_text_font(l, th->small, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(th->text), 0);
        lv_label_set_text(l, title);
        lv_obj_set_pos(l, 14, 8);
        char sub[32];
        int q_n = (c->n + 1) / 2, d = q_n % 10, h = q_n % 100;
        snprintf(sub, sizeof sub, "%d %s", q_n,
                 d == 1 && h != 11 ? "вопрос" : d >= 2 && d <= 4 && (h < 12 || h > 14) ? "вопроса" : "вопросов");
        th_text_at(p, th->tiny, th->dim, sub, 14, 38);
    }
}

void ask_list_open(lv_obj_t *scr)
{
    a.list = scr;
    list_build();
}

void ask_list_close(void) { a.list = NULL; }
bool ask_list_button(int id) { (void)id; return false; } /* the shell's: the bottom key goes back */

/* ---------- the states ---------- */
static void go(int state)
{
    a.state = state;
    a.since = lv_tick_get();
    if (!a.t) a.t = lv_timer_create(tick, 100, NULL);
    if (state != ASK_LISTEN) a.streaming = false;
    if (state == ASK_IDLE || state == ASK_CHAT) strip_hide();
    pip_info_t in;
    pip_get_info(&in);
    if (state != ASK_LISTEN && state != ASK_THINK && (!strcmp(in.reaction, "listen") || !strcmp(in.reaction, "think")))
        pip_react(PIP_NEUTRAL, 0.01f, ""); /* ours: over */
    if (state == ASK_IDLE) {
        if (a.answering && a.answer && a.len && a.work) chat_push(a.work, 'A', a.answer), chat_keep(); /* left mid-answer: keep what came */
        a.answering = false, a.fail[0] = 0;
        page_hide();
        speech_stop();
        if (a.list) list_build();
    }
}

static void fail(const char *why)
{
    copy(a.fail, sizeof a.fail, why);
    if (a.state == ASK_LISTEN) send(ASK_CANCEL);
    go(ASK_CHAT);
    page_build();
    pip_react(PIP_PUZZLED, 2.5f, "no answer");
}

static void think(void)
{
    a.streaming = false;
    send(ASK_STOP);
    go(ASK_THINK);
    if (a.o) page_build();
    else strip_show("думаю…"), pip_react(PIP_SKEPTICAL, THINK_MAX_MS / 1000.0f, "think");
}

static void tick(lv_timer_t *t)
{
    (void)t;
    uint32_t in = lv_tick_elaps(a.since);
    bool held = a.state == ASK_LISTEN && a.down && lv_tick_elaps(a.down_at) >= TAP_MS;
    if (held && !a.in_chat && !a.strip) strip_show("слушаю…");
    if (a.state == ASK_LISTEN && in > LISTEN_MAX_MS) a.down = false, think(); /* he can only hold his breath so long */
    else if (a.state == ASK_THINK && in > THINK_MAX_MS) fail("Ответ не пришёл. Попробуй ещё раз.");
    if (a.state == ASK_LISTEN && !a.in_chat) pip_react(PIP_CURIOUS, 1, "listen"); /* held for as long as he listens */
    if (speech_busy() != a.spoke) a.spoke = speech_busy(), page_hint();
}

/* the key went down: listening starts at once (the first word often comes with the press) */
static void listen(bool in_chat)
{
    a.down = true, a.down_at = lv_tick_get(), a.in_chat = in_chat;
    if (a.answering && a.answer && a.len && a.work) chat_push(a.work, 'A', a.answer), chat_keep();
    a.heard[0] = a.fail[0] = 0, a.q_done = a.answering = false, a.len = 0;
    if (!in_chat) chat_new(); /* from his face: a new conversation */
    speech_stop();
    if (!a.app) return; /* decided when it is let go: a tap blinks (or goes back), a hold says the app is missing */
    go(ASK_LISTEN);
    send(ASK_START);    /* first: the host sends the conversation and gets ready for the voice */
    a.streaming = true;
    if (a.o) page_build(); /* "слушаю…" at the bottom of the chat */
}

bool ask_press(void)
{
    if (a.state != ASK_IDLE) return false;
    listen(false);
    return true;
}

void ask_release(void)
{
    if (!a.down) return;
    a.down = false;
    bool tap = lv_tick_elaps(a.down_at) < TAP_MS;
    if (a.state == ASK_LISTEN) {
        if (tap) { /* a tap: no question */
            a.streaming = false;
            send(ASK_CANCEL);
            go(ASK_IDLE);
            if (!a.in_chat) pip_button(PIP_BTN_BOOT); /* on his face: the blink it always was; in a chat: back */
        } else think();
    } else if (tap) { /* no app: a tap is what it always was */
        if (a.in_chat) go(ASK_IDLE);
        else pip_button(PIP_BTN_BOOT);
    } else {
        work();
        fail("Нет связи с iPhone. Открой Pip Bridge.");
    }
}

bool ask_button(int id)
{
    switch (a.state) {
    case ASK_CHAT:
        if (id == PIP_BTN_BOOT) listen(true); /* held: more in this chat; a tap goes back (on release) */
        else if (speech_busy()) speech_stop(), send(ASK_HUSH), page_hint();
        else if (!a.answering && *ask_say_text() && a.app) send(ASK_SAY), pip_react(PIP_HAPPY, 1.5f, "reads"), page_hint();
        return true;
    case ASK_THINK:
        if (id == PIP_BTN_BOOT) { /* never mind */
            send(ASK_CANCEL);
            if (a.o) a.heard[0] = 0, go(ASK_CHAT), page_build();
            else go(ASK_IDLE);
        }
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
    if (a.o && !a.q_done) page_build();
}

void ask_answer(const char *piece)
{
    if (a.state != ASK_THINK && !(a.state == ASK_CHAT && a.answering)) return; /* cancelled: what comes late is dropped */
    if (!a.answer && !(a.answer = malloc(ANSWER_MAX))) return;
    if (a.state == ASK_THINK) {
        if (work()) chat_push(a.work, 'Q', a.heard[0] ? a.heard : "…");
        a.q_done = a.answering = true, a.len = 0, a.answer[0] = 0, a.fail[0] = 0;
        go(ASK_CHAT);
        pip_react(PIP_HAPPY, 1.5f, "answer");
    }
    const char *p = piece;
    for (; *p && a.len < ANSWER_MAX - 1; p++) a.answer[a.len++] = *p;
    if (((unsigned char)*p & 0xC0) == 0x80) { /* full in the middle of a letter: that letter goes */
        while (a.len > 0 && ((unsigned char)a.answer[a.len - 1] & 0xC0) == 0x80) a.len--;
        if (a.len > 0) a.len--;
    }
    a.answer[a.len] = 0;
    page_build();
}

void ask_done(void)
{
    if (a.state == ASK_THINK) ask_answer("…"); /* an empty answer still closes the question */
    if (a.state != ASK_CHAT || !a.answering) return;
    if (a.work) chat_push(a.work, 'A', a.answer);
    a.answering = false;
    chat_keep();
    page_build();
}

void ask_fail(const char *why)
{
    if (a.state == ASK_LISTEN || a.state == ASK_THINK) fail(why && *why ? why : "Что-то пошло не так.");
}
