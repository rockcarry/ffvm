#ifndef __ETHPHY_H__
#define __ETHPHY_H__

typedef void (*PFN_ETHPHY_CALLBACK)(void *cbctx, char *buf, int len);

void* ethphy_open (char *ifname, PFN_ETHPHY_CALLBACK callback, void *cbctx);
void  ethphy_close(void *ctx);
int   ethphy_send (void *ctx, char *buf, int len);

#endif
