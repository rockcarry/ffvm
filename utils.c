#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <windows.h>
#include <conio.h>
#include "utils.h"

uint32_t get_tick_count(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static pthread_mutex_t s_lock = (pthread_mutex_t)NULL;
static pthread_t    s_hthread = (pthread_t      )NULL;
#define FLAG_EXIT  (1 << 0)
#define FLAG_PAUSE (1 << 1)
static int s_flags = 0;
static int s_head, s_tail, s_size;
static int s_buff[256];

static void* console_thread_proc(void *arg)
{
    while (!(s_flags & FLAG_EXIT)) {
        if (s_flags & FLAG_PAUSE) { usleep(100 * 1000); continue; }
        int c = fgetc(stdin);
        pthread_mutex_lock(&s_lock);
        if (!(s_flags & FLAG_PAUSE) && s_size < sizeof(s_buff)) {
            s_buff[s_tail] = c;
            s_tail = (s_tail + 1) % sizeof(s_buff); s_size++;
        }
        pthread_mutex_unlock(&s_lock);
    }
    return NULL;
}

static void console_thread_signal(void)
{
    DWORD        n   = 0;
    INPUT_RECORD rec = {};
    rec.EventType                      = KEY_EVENT;
    rec.Event.KeyEvent.bKeyDown        = TRUE;
    rec.Event.KeyEvent.wRepeatCount    = 1;
    rec.Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
    rec.Event.KeyEvent.uChar.AsciiChar = '\r';
    WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &rec, 1, &n);
    rec.Event.KeyEvent.uChar.AsciiChar = '\n';
    WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &rec, 1, &n);
}

static void console_thread_pause(void)
{
    if (!(s_flags & FLAG_PAUSE)) {
        pthread_mutex_lock(&s_lock);
        s_flags |= FLAG_PAUSE;
        console_thread_signal();
        pthread_mutex_unlock(&s_lock);
    }
}

void console_init(void)
{
    if (!s_lock) pthread_mutex_init(&s_lock, NULL);
    if (!s_hthread) { s_flags = 0; pthread_create(&s_hthread, NULL, console_thread_proc, NULL); }
}

void console_exit(void)
{
    pthread_mutex_lock(&s_lock);
    s_flags |= FLAG_EXIT;
    console_thread_signal();
    pthread_mutex_unlock(&s_lock);
    if (s_hthread) { pthread_join(s_hthread, NULL);  s_hthread = (pthread_t      )NULL; }
    if (s_lock   ) { pthread_mutex_destroy(&s_lock); s_lock    = (pthread_mutex_t)NULL; }
}

int console_getc(void)
{
    int c = EOF;
    pthread_mutex_lock(&s_lock);
    s_flags &= ~FLAG_PAUSE;
    if (s_size) {
        c = s_buff[s_head];
        s_head = (s_head + 1) % sizeof(s_buff); s_size--;
    }
    pthread_mutex_unlock(&s_lock);
    return c;
}

int  console_getch (void) { console_thread_pause(); return getch(); }
int  console_kbhit (void) { console_thread_pause(); return kbhit(); }
void console_clrscr(void) { system("cls");  }

void console_gotoxy(int x, int y)
{
    COORD coord = { .X = x, .Y = y };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}
