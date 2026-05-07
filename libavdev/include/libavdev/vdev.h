#ifndef __LIBAVDEV_VDEV__
#define __LIBAVDEV_VDEV__

#include <stdint.h>

#ifndef DEFINE_TYPE_BITMAP
#define DEFINE_TYPE_BITMAP
typedef struct { // note: this struct must keep same as libfuncs/bitmap.h 
    int       width;
    int       height;
    int       stride;
    int       cdepth;
    uint8_t  *pdata;
    uint32_t *ppal;
    void (*setpixel)(void *pb, int x, int y, int c);
    int  (*getpixel)(void *pb, int x, int y);
} BMP;
#endif

enum {
    VDEV_CALLBACK_VDEV_INITED = 0x10170000,
    VDEV_CALLBACK_VDEV_RESIZE,
    VDEV_CALLBACK_VDEV_PAINT ,
    VDEV_CALLBACK_VDEV_CLOSED,
};

typedef long (*PFN_VDEV_CB)(void *cbctx, int type, void *buf, int size);

void* vdev_init(void *params, PFN_VDEV_CB callback, void *cbctx); // params: example: "surfaces:3,resize,w:640,h:480", if NULL return NULL
void  vdev_exit(void *ctx);

BMP*  vdev_lock  (void *ctx, int idx);
void  vdev_unlock(void *ctx, int idx);
void  vdev_render(void *ctx);

long  vdev_set (void *ctx, char *key, void *val);
long  vdev_get (void *ctx, char *key, void *val);
void  vdev_dump(void *ctx, char *str, int len, int page);

#define VDEV_KEY_STATE           "i_state"            // set/get, get VDEV_CALLBACK_VDEV_CLOSED - indicate window closed
#define VDEV_KEY_WIDTH           "i_width"            // get only, return vdev width
#define VDEV_KEY_HEIGHT          "i_height"           // get only, return vdev height
#define VDEV_KEY_SURFACE_PARAMS  "s_surface_params0"  // set, surface params
                                                      // example: "pixfmt:argb,w:640,h:480,x:center,y:center"
                                                      //          "parent:1,w:0.5,h:0.1,x:center,y:0"
                                                      //          "w:0.999999,h:8,x:center,y:-0.000001"
                                                      // pixfmt can be "argb", "yuyv", "uyvy", "nv12", "nv21", "yuv420p"
#define VDEV_KEY_IDEV            "p_idev"             // get only, get idev context
#define VDEV_KEY_HWND            "p_hwnd"             // get only, get vdev hwnd

#endif
