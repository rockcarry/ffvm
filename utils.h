#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>

uint32_t get_tick_count(void);

int  console_getch (void);
int  console_kbhit (void);
void console_clrscr(void);
void console_gotoxy(int x, int y);

#endif
