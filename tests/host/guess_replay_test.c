#include "guess_replay.h"
#include <assert.h>
#include <stdio.h>
int main(void){GuessReplay h={0};
 assert(guess_replay_can_play(&h,40.7128,-74.006,1));
 guess_replay_add(&h,40.7128,-74.006);
 assert(!guess_replay_can_play(&h,40.713,-74.006,1));
 assert(guess_replay_can_play(&h,39,-74,1));
 guess_replay_add(&h,0,179.999);
 assert(!guess_replay_can_play(&h,0,-179.999,1));
 for(int i=0;i<500;i++)guess_replay_add(&h,(i%120)-60.,(i%330)-160.);
 assert(h.count==GUESS_REPLAY_MAX&&h.cursor==118);
 assert(guess_replay_save(&h,"/tmp/guess-history.dat"));
 GuessReplay q={0};assert(guess_replay_load(&q,"/tmp/guess-history.dat"));
 assert(q.count==h.count && q.cursor==h.cursor);
 assert(!guess_replay_can_play(&q,999,0,1));
 remove("/tmp/guess-history.dat");puts("StreetGuess persistent anti-repeat history tests passed");return 0;
}
