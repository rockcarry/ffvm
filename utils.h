#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>

uint64_t get_tick_count(void);

void console_init  (void);
void console_exit  (void);
int  console_getc  (void);
int  console_getch (void);
int  console_kbhit (void);
void console_clrscr(void);
void console_gotoxy(int x, int y);

#endif
