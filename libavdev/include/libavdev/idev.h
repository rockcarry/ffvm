#ifndef __LIBAVDEV_IDEV__
#define __LIBAVDEV_IDEV__

#include <stdint.h>

enum {
    IDEV_CALLBACK_KEY_EVENT = 0x10180000,
    IDEV_CALLBACK_MOUSE_EVENT,
};

typedef long (*PFN_IDEV_CB)(void *cbctx, int type, void *buf, int len);

#ifndef DEFINE_TYPE_IDEV
#define DEFINE_TYPE_IDEV
typedef struct {
    uint32_t  key_bits[8];
    int32_t   mouse_x, mouse_y, mouse_btns;
    PFN_IDEV_CB callback;
    void       *cbctx;
} IDEV;
#endif

void* idev_init(void *params, PFN_IDEV_CB callback, void *cbctx);
void  idev_exit(void *ctx);
long  idev_set (void *ctx, char *key, void *val);
long  idev_get (void *ctx, char *key, void *val);
void  idev_dump(void *ctx, char *str, int len, int page);

int   idev_getkey  (void *ctx, int key);
void  idev_getmouse(void *ctx, int *x, int *y, int *btns);

#endif

