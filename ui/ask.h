/* The assistant: hold the bottom key on Pip's face, speak, let go. The voice goes to the iPhone app (ios/PipBridge),
   which turns it into text on the phone, asks Claude and sends the answer back; it shows as text on the screen.
   Pip listens (curious) while the key is held and thinks (puzzled) until the answer comes.
   Portable: the host carries the audio and the lines (firmware: ble.c's bridge service; simulator: fakephone.c). */
#pragma once
#include <stdbool.h>

enum { ASK_IDLE, ASK_LISTEN, ASK_THINK, ASK_ANSWER, ASK_FAIL };
enum { ASK_START, ASK_STOP, ASK_CANCEL, ASK_SAY }; /* UI -> host: start/stop the voice, never mind, read it out on the phone */

/* ---------- host -> UI ---------- */
void ask_app(bool on);              /* the bridge app on the iPhone is there (it subscribed) or gone */
bool ask_app_on(void);
void ask_heard(const char *text);   /* what the phone made of the voice so far */
void ask_answer(const char *piece); /* a piece of the answer; '\t' is a new line */
void ask_done(void);                /* the whole answer is here */
void ask_fail(const char *why);     /* no answer: the reason in a few words */

/* ---------- UI -> host ---------- */
void ask_on_host(void (*cb)(int what));
bool ask_streaming(void); /* the microphone's 16 kHz goes to the phone now: a plain flag, any task may read it */

/* ---------- for the shell ---------- */
bool ask_press(void);         /* the bottom key went down on Pip's face: true = the assistant took it */
void ask_release(void);       /* the bottom key let go */
bool ask_button(int id);      /* while ask_where(): the side buttons are the assistant's */
const char *ask_where(void);  /* "ask" while listening, thinking or showing an answer; NULL otherwise */
int ask_state(void);
const char *ask_state_name(void);
