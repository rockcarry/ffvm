#ifndef __LIBAVDEV_ACAP__
#define __LIBAVDEV_ACAP__

#include <stdint.h>

#define ACAP_CALLBACK_DATA 0x10000000

enum {
    ACAP_FRAME_TYPE_RAW , // raw, pcm which without audio 3a process
    ACAP_FRAME_TYPE_PCM , // pcm, pcm which with audio 3a process
    ACAP_FRAME_TYPE_ALAW, // alaw
    ACAP_FRAME_TYPE_ULAW, // ulaw
};

typedef struct {
    int32_t  type;
    uint32_t ts;
    uint8_t *buf;
    int      len;
} ACAP_FRAME;

typedef long (*PFN_ACAP_CB)(void *cbctx, int type, void *buf, int size);

// params: example "samprate:16000,chnnum:1,frmsize:512,frmnum:16", if "" use default as example, if NULL return NULL
//         on ingenic soc, frmsize should be integer multiple of 10ms (ex. 8KHz samprate, frmsize should be 80 * N), otherwise will cause crash.
void* acap_init(void *params, PFN_ACAP_CB callback, void *cbctx);
void  acap_exit(void *ctx);
long  acap_set (void *ctx, char *key, void *val);
long  acap_get (void *ctx, char *key, void *val);
void  acap_dump(void *ctx, char *str, int len, int page);

#define ACAP_KEY_STATE     "i_state"     // set/get
#define ACAP_KEY_GAIN      "i_gain"      // set/get, mic gain
#define ACAP_KEY_EN_RAW    "i_en_raw"    // set/get, enable/disable raw data
#define ACAP_KEY_EN_PCM    "i_en_pcm"    // set/get, enable/disable pcm data
#define ACAP_KEY_EN_ALAW   "i_en_alaw"   // set/get, enable/disable alaw data
#define ACAP_KEY_EN_ULAW   "i_en_ulaw"   // set/get, enable/disable ulaw data
#define ACAP_KEY_EN_AEC    "i_en_aec"    // set/get, enable/disable aec proccess, need restart
#define ACAP_KEY_EN_ALGO   "i_en_algo"   // set/get, enable/disable ingenic audio ai algorithm proccess, need restart
#define ACAP_KEY_EN_ALL    "i_en_all"    // set/get, enable/disable all data, pcm + alaw + ulaw
#define ACAP_KEY_SAMPRATE  "i_samprate"  // get only
#define ACAP_KEY_FRMSIZE   "i_frmsize"   // get only
#define ACAP_KEY_CFGINDEX  "i_cfgindex"  // set/get, current in use audio config index, only supported on ssc3xx platform
#define ACAP_KEY_DUMPCFG   "i_dumpcfg"   // set only dump audio config

#endif
