#ifndef CONSOLE_H
#define CONSOLE_H

/* Reads command lines from the game's terminal (stdin) on a background
   thread so the render loop never blocks. Call Con_Start() once after
   startup, then drain with Con_Poll() each frame. */
void Con_Start(void);
int  Con_Poll(char *out, int cap);   /* 1 if a line was dequeued, else 0 */

#endif
