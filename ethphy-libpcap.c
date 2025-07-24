#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <pcap.h>
#include "ethphy.h"

typedef struct {
    HMODULE   hDll;
    pcap_t   *pcap;
    pcap_t* (*pcap_open_live)(const char*, int, int, int, char*);
    void    (*pcap_close)(pcap_t*);
    int     (*pcap_findalldevs)(pcap_if_t**, char*);
    void    (*pcap_freealldevs)(pcap_if_t*);
    int     (*pcap_loop)(pcap_t*, int, pcap_handler, u_char*);
    void    (*pcap_breakloop)(pcap_t*);
    int     (*pcap_sendpacket)(pcap_t*, const u_char*, int);
    char*   (*pcap_geterr)(pcap_t*);

    #define FLAG_EXIT (1 << 0)
    uint32_t  flags;
    pthread_t thread;
    PFN_ETHPHY_CALLBACK callback;
    void               *cbctx;
} PHYDEV;

static void packet_handler(u_char *param, const struct pcap_pkthdr *header, const u_char *pkt_data)
{
    PHYDEV *phy = (PHYDEV*)param;
    if (phy->callback) phy->callback(phy->cbctx, (char*)pkt_data, header->len);
}

static void* ethphy_work_proc(void *arg)
{
    PHYDEV *phy = arg;
    while (!(phy->flags & FLAG_EXIT)) {
        phy->pcap_loop(phy->pcap, 0, packet_handler, (u_char*)phy);
    }
    return NULL;
}

void* ethphy_open(char *ifname, PFN_ETHPHY_CALLBACK callback, void *cbctx)
{
    PHYDEV *phy = calloc(1, sizeof(PHYDEV));
    if (!phy) return NULL;

    pcap_t    *pcap    = NULL;
    pcap_if_t *alldevs = NULL, *d;
    char errbuf[PCAP_ERRBUF_SIZE];
    int  i = 0, n = -1;

    phy->hDll = LoadLibrary(TEXT("wpcap.dll"));
    if (!phy->hDll) {
        fprintf(stderr, "Error in load wpcap.dll library !\n");
        goto failed;
    }

    phy->pcap_open_live   = (void*)GetProcAddress(phy->hDll, "pcap_open_live");
    phy->pcap_close       = (void*)GetProcAddress(phy->hDll, "pcap_close");
    phy->pcap_findalldevs = (void*)GetProcAddress(phy->hDll, "pcap_findalldevs");
    phy->pcap_freealldevs = (void*)GetProcAddress(phy->hDll, "pcap_freealldevs");
    phy->pcap_loop        = (void*)GetProcAddress(phy->hDll, "pcap_loop");
    phy->pcap_breakloop   = (void*)GetProcAddress(phy->hDll, "pcap_breakloop");
    phy->pcap_sendpacket  = (void*)GetProcAddress(phy->hDll, "pcap_sendpacket");
    phy->pcap_geterr      = (void*)GetProcAddress(phy->hDll, "pcap_geterr");
    if (  !phy->pcap_open_live || !phy->pcap_findalldevs || !phy->pcap_freealldevs || !phy->pcap_loop
       || !phy->pcap_breakloop || !phy->pcap_sendpacket || !phy->pcap_geterr)
    {
        fprintf(stderr, "Error in GetProcAddress !\n");
        goto failed;
    }

    if (phy->pcap_findalldevs(&alldevs, errbuf) == -1) {
        fprintf(stderr, "Error in pcap_findalldevs: %s\n", errbuf);
        goto failed;
    }

    for (d = alldevs; d; d = d->next) {
        printf("%d. %s", ++i, d->name);
        if (ifname && strlen(ifname) > 38 && strstr(d->name, ifname)) { n = i; break; }
        if (d->description) {
            printf(" (%s)\n", d->description);
        } else {
            printf(" (No description available)\n");
        }
    }

    if (i == 0) {
        printf("\nNo interfaces found ! Make sure WinPcap is installed.\n");
        goto failed;
    }

    if (n == -1) {
        printf("Enter the interface number (1-%d):", i);
        scanf("%d", &n);
    }

    if (n < 1 || n > i) {
        printf("\nInterface number out of range.\n");
        goto failed;
    }

    /* Jump to the selected adapter */
    for (d = alldevs, i = 0; i < n - 1; d = d->next, i++);

    /* Open the adapter */
    if ((pcap = phy->pcap_open_live(d->name,
        65536, // portion of the packet to capture.
        1,     // promiscuous mode (nonzero means promiscuous)
        1,     // read timeout, 0 blocked, -1 no timeout
        errbuf // error buffer
        )) == NULL)
    {
        fprintf(stderr, "\nUnable to open the adapter. %s is not supported by WinPcap\n", d->name);
        goto failed;
    }

    printf("\nlistening on %s...\n", d->description);
    phy->pcap_freealldevs(alldevs);

    phy->pcap     = pcap;
    phy->callback = callback;
    phy->cbctx    = cbctx;
    pthread_create(&phy->thread, 0, ethphy_work_proc, phy);
    return phy;

failed:
    if (alldevs) phy->pcap_freealldevs(alldevs);
    if (phy->hDll) CloseHandle(phy->hDll);
    free(phy);
    return NULL;
}

void ethphy_close(void *ctx)
{
    PHYDEV *phy = ctx;
    if (phy) {
        phy->flags |= FLAG_EXIT;
        if (phy->pcap  ) phy->pcap_breakloop(phy->pcap);
        if (phy->thread) pthread_join(phy->thread, NULL);
        if (phy->pcap  ) phy->pcap_close(phy->pcap);
        if (phy->hDll  ) FreeLibrary(phy->hDll);
        free(phy);
    }
}

int ethphy_send(void *ctx, char *buf, int len)
{
    if (!ctx) return -1;
    PHYDEV *phy = ctx;
    if (phy->pcap_sendpacket(phy->pcap, (const u_char*)buf, len) != 0) {
        fprintf(stderr, "\nError sending the packet: %s\n", phy->pcap_geterr(phy->pcap));
        return -1;
    }
    return 0;
}
