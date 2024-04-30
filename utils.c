#include <windows.h>
#include <conio.h>
#include <time.h>
#include <unistd.h>
#include "utils.h"

uint32_t get_tick_count(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int  console_getch (void) { return getch(); }
int  console_kbhit (void) { return kbhit(); }
void console_clrscr(void) { system("cls");  }

void console_gotoxy(int x, int y)
{
    COORD coord;
    coord.X = x;
    coord.Y = y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}
