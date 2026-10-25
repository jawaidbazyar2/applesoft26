#ifndef AS_MACHINE_H
#define AS_MACHINE_H

#include "as_program.h"
#include "as_screen.h"

typedef struct AsMachine AsMachine;

AsMachine *as_machine_new(AsProgram *program, AsScreen *screen);
void as_machine_free(AsMachine *m);

void as_machine_run(AsMachine *m);
void as_machine_stop(AsMachine *m);
void as_machine_pause(AsMachine *m);
void as_machine_cont(AsMachine *m);
void as_machine_step(AsMachine *m);
int as_machine_quantum(AsMachine *m, double seconds);

int as_machine_running(const AsMachine *m);
int as_machine_paused(const AsMachine *m);
int as_machine_waiting(const AsMachine *m);
uint16_t as_machine_line(const AsMachine *m);
const char *as_machine_status(const AsMachine *m);

/* GOSUB callers, outermost first. Skips FOR frames and immediate-mode lines.
   Writes min(depth, cap) line numbers. *count is the full caller depth. */
void as_machine_gosub_stack(const AsMachine *m, uint16_t *numbers, int cap, int *count);

/* Keyboard from the run window. Echoes INPUT, satisfies GET, else sets the strobe. */
void as_machine_keydown(AsMachine *m, int ch);

/* Drop CONT after the source changes. Breakpoints do not call this. */
void as_machine_edited(AsMachine *m);

/* Syntax-check one tokenized line. err_at receives a token index on failure. */
int as_check_line(const uint8_t *tokens, size_t len, size_t *err_at);

/* 1 when execution is on that program line. Immediate commands are not a line. */
int as_machine_on_line(const AsMachine *m, uint16_t number);

/* Apple II prompt. A leading line number stores the line (a bare number deletes it).
   Anything else runs now and keeps variables.
   *kind is 0 empty, 1 stored, 2 executed. Line-number errors return AS_ERR_SYNTAX. */
int as_machine_prompt(AsMachine *m, const char *text, int *kind);

#endif
