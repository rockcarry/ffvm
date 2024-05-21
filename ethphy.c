#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <pcap.h>
#include "ethphy.h"

typedef struct {
    pcap_t   *pcap;
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
        pcap_loop(phy->pcap, 0, packet_handler, (u_char*)phy);
    }
    return NULL;
}

void* ethphy_open(int dev, PFN_ETHPHY_CALLBACK callback, void *cbctx)
{
    pcap_t    *pcap = NULL;
    pcap_if_t *alldevs, *d;
    char errbuf[PCAP_ERRBUF_SIZE];
    int  i = 0, n;

    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
        fprintf(stderr, "Error in pcap_findalldevs: %s\n", errbuf);
        return NULL;
    }

    for (d = alldevs; d; d = d->next) {
        printf("%d. %s", ++i, d->name);
        if (d->description) {
            printf(" (%s)\n", d->description);
        } else {
            printf(" (No description available)\n");
        }
    }

    if (i == 0) {
        printf("\nNo interfaces found ! Make sure WinPcap is installed.\n");
        return NULL;
    }

    if (dev < 1 || dev > i) {
        printf("Enter the interface number (1-%d):", i);
        scanf("%d", &n);
    } else {
        n = dev;
    }

    if (n < 1 || n > i) {
        printf("\nInterface number out of range.\n");
        pcap_freealldevs(alldevs);
        return NULL;
    }

    /* Jump to the selected adapter */
    for (d = alldevs, i = 0; i < n - 1; d = d->next, i++);

    /* Open the adapter */
    if ((pcap = pcap_open_live(d->name,
        65536, // portion of the packet to capture.
        1,     // promiscuous mode (nonzero means promiscuous)
        1,     // read timeout, 0 blocked, -1 no timeout
        errbuf // error buffer
        )) == NULL)
    {
        fprintf(stderr, "\nUnable to open the adapter. %s is not supported by WinPcap\n", d->name);
        pcap_freealldevs(alldevs);
        return NULL;
    }

    printf("\nlistening on %s...\n", d->description);
    pcap_freealldevs(alldevs);

    PHYDEV *phy = calloc(1, sizeof(PHYDEV));
    if (!phy) { pcap_close(pcap); return NULL; }

    phy->pcap     = pcap;
    phy->callback = callback;
    phy->cbctx    = cbctx;
    pthread_create(&phy->thread, 0, ethphy_work_proc, phy);
    return phy;
}

void ethphy_close(void *ctx)
{
    PHYDEV *phy = ctx;
    if (phy) {
        phy->flags |= FLAG_EXIT;
        pcap_breakloop(phy->pcap);
        pthread_join(phy->thread, NULL);
        pcap_close(phy->pcap);
        free(phy);
    }
}

int ethphy_send(void *ctx, char *buf, int len)
{
    if (!ctx) return -1;
    PHYDEV *phy = ctx;
    if (pcap_sendpacket(phy->pcap, (const u_char*)buf, len) != 0) {
        fprintf(stderr, "\nError sending the packet: %s\n", pcap_geterr(phy->pcap));
        return -1;
    }
    return 0;
}
