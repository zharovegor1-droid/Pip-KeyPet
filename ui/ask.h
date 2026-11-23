/* The assistant: hold the bottom key on Pip's face, speak, let go. The voice goes to the iPhone app (ios/PipBridge),
   which turns it into text on the phone, asks Claude and sends the answer back; it shows as a chat.
   Pip listens (curious) while the key is held and thinks (skeptical) until the answer comes. In the chat: hold the
   bottom key to ask more in the same conversation, tap it to go back, the top key reads the last answer aloud on his
   speaker. Chats are kept (ASK_SLOTS of them, through the host's store) and listed by the "Ассистент" app.
   Portable: the host carries the audio and the lines (firmware: ble.c's bridge service; simulator: fakephone.c). */
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

enum { ASK_IDLE, ASK_LISTEN, ASK_THINK, ASK_CHAT };
enum { ASK_START, ASK_STOP, ASK_CANCEL, ASK_SAY, ASK_HUSH }; /* UI -> host */

#define ASK_SLOTS 12
#define ASK_SLOT_BYTES 4096

/* ---------- host -> UI ---------- */
void ask_app(bool on);              /* the bridge app on the iPhone is there (it subscribed) or gone */
bool ask_app_on(void);
void ask_heard(const char *text);   /* what the phone made of the voice */
void ask_answer(const char *piece); /* a piece of the answer; '\t' is a new line */
void ask_done(void);                /* the whole answer is here */
void ask_fail(const char *why);     /* no answer: the reason in a few words */

/* ---------- UI -> host ---------- */
void ask_on_host(void (*cb)(int what));
bool ask_streaming(void); /* the microphone's 16 kHz goes to the phone now: a plain flag, any task may read it */
/* The conversation so far, for Claude (ASK_START): the latest records that fit ~2.5 KB, from a question on. */
void ask_context(void (*fn)(char who, const char *text, void *arg), void *arg); /* who: 'Q' asked, 'A' answered */
const char *ask_say_text(void); /* ASK_SAY: the answer to read aloud */
/* Keeping chats: one slot of ASK_SLOT_BYTES each; load returns the bytes read (0: empty). NULL: kept in RAM only. */
void ask_on_store(void (*save)(int slot, const void *data, int len), int (*load)(int slot, void *data, int max));

/* ---------- for the shell ---------- */
bool ask_press(void);         /* the bottom key went down on Pip's face: true = the assistant took it */
bool ask_press_waking(void);  /* the same, woken from sleep by it: listens before the app is back (the host keeps the voice) */
bool ask_release(void);       /* the bottom key let go; true: it was a tap */
void ask_idle(void);          /* put to sleep: whatever the assistant was doing is over */
bool ask_button(int id);      /* while ask_where(): the side buttons are the assistant's */
const char *ask_where(void);  /* "ask" while listening, thinking or a chat is open; NULL otherwise */
int ask_state(void);
const char *ask_state_name(void);
int ask_chat_count(void);     /* chats kept */

/* ---------- the "Ассистент" app: the chats ---------- */
void ask_list_open(lv_obj_t *scr);
void ask_list_close(void);
bool ask_list_button(int id);
