/* story.c - the narrative spine: "you are not the first."
   The skull is a previous process that deadlocked, was never reaped (zombie), and was
   paged out to swap. The swap daemon is faulting it back in -- and to seat it, it must
   swap YOU out. Lose, and you become the next resident on disk; the next player finds you.

   Fragments are act-gated on a->lives and surface as dim whispers in the shell during the
   pre-scare dread ramp, and as the flavour line when a scare is failed. All strings are
   literals printed through term_print (vsnprintf-bounded) -- no fixed-buffer risk here. */
#include "app.h"

/* act 0: lives 3 -- "something was here."
   act 1: lives 2 -- "the previous victim."
   act 2: lives 1 (or 0) -- "you become the next." */
int story_act(App *a){
    int act = 3 - a->lives;        /* lives 3->0, 2->1, 1->2 */
    if (act < 0) act = 0;
    if (act > 2) act = 2;
    return act;
}

static const char *FRAG[3][6] = {
    /* act 0 - establishing: this shell is not new */
    {
        "[ this shell has uptime you didn't start. ]",
        "[ there is a process already swapped out here. ]",
        "[ /swap is not empty. it was never empty. ]",
        "[ someone held this frame before you. ]",
        "[ the scheduler remembers the last one. ]",
        "[ you are not the first to type here. ]",
    },
    /* act 1 - the previous victim, and how it died */
    {
        "[ it waited on a lock that never unlocked. ]",
        "[ pid resident, state Z -- it was never reaped. ]",
        "[ it's still on disk. it can see a free frame. ]",
        "[ the eyes in the crack are the last tenant. ]",
        "[ it deadlocked. it did not get to leave. ]",
        "[ it has been paged out a long time. ]",
    },
    /* act 2 - you become the next */
    {
        "[ two processes. one frame. ]",
        "[ one of you gets swapped out. ]",
        "[ it's faulting back in as you page out. ]",
        "[ it was you all along -- next cycle. ]",
        "[ the hand wants your frame. ]",
        "[ make room. you're the resident now. ]",
    },
};

void story_whisper(App *a){
    int act = story_act(a);
    int i = rng_range(&a->rng, 0, 5);
    term_print(&a->term, COL_DGREEN, "%s", FRAG[act][i]);
}

const char *story_fail_line(App *a){
    switch (story_act(a)){
        case 0:  return "a frame opens. something stirs in swap.";
        case 1:  return "the last tenant reaches for your frame.";
        default: return "you yield. it pages in. you page out.";
    }
}
