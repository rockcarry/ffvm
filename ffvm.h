#ifndef __FFVM_H__
#define __FFVM_H__

void* ffvm_init (int memsize);
void  ffvm_exit (void *ctx);
void  ffvm_reset(void *ctx);
int   ffvm_run  (void *ctx);

#endif
