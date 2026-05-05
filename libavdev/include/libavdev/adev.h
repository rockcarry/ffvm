#ifndef __LIVAVDEV_ADEV__
#define __LIVAVDEV_ADEV__

typedef long (*PFN_ADEV_CB)(void *cbctx, int type, void *buf, int size);

// params: example "samprate:16000,chnnum:1,frmsize:512,frmnum:16", if "" use default as example, if NULL return NULL
//         on ingenic soc, frmsize should be integer multiple of 10ms (ex. 8KHz samprate, frmsize should be 80 * N), otherwise will cause crash.
void* adev_init(void *params, PFN_ADEV_CB callback, void *cbctx);
void  adev_exit(void *ctx);
int   adev_play(void *ctx, void *buf, int len, int waitms);
long  adev_set (void *ctx, char *key, void *val);
long  adev_get (void *ctx, char *key, void *val);
void  adev_dump(void *ctx, char *str, int len, int page);

#define ADEV_KEY_GAIN       "i_gain"      // set/get, speaker gain
#define ADEV_KEY_BUFFSIZE   "i_buffsize"  // set/get, buffer size
#define ADEV_KEY_SAMPRATE   "i_samprate"  // get only
#define ADEV_KEY_EN_DH      "i_en_dh"     // set/get, enable/disable dh function

#endif
