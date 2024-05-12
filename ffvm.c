#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <libavdev/adev.h>
#include <libavdev/vdev.h>
#include <libavdev/idev.h>
#include "utils.h"

#define FFVM_ADEV_MAX_BUFNUM      5

#define RISCV_CPU_FREQ_MAX       (100*1000*1000)
#define RISCV_FRAMERATE           100
#define RISCV_DISK_SECTSIZE       512

#define REG_FFVM_STDIO            0xFF000000
#define REG_FFVM_STDERR           0xFF000004
#define REG_FFVM_GETCH            0xFF000008
#define REG_FFVM_KBHIT            0xFF00000C
#define REG_FFVM_CLRSCR           0xFF000010
#define REG_FFVM_GOTOXY           0xFF000014

#define REG_FFVM_KEYBD1           0xFF000110
#define REG_FFVM_KEYBD2           0xFF000114
#define REG_FFVM_KEYBD3           0xFF000118
#define REG_FFVM_KEYBD4           0xFF00011C
#define REG_FFVM_MOUSE_XY         0xFF000120
#define REG_FFVM_MOUSE_BTN        0xFF000124

#define REG_FFVM_DISP_WH          0xFF000200
#define REG_FFVM_DISP_ADDR        0xFF000204
#define REG_FFVM_DISP_REFRESH_XY  0xFF000208
#define REG_FFVM_DISP_REFRESH_WH  0xFF00020C
#define REG_FFVM_DISP_REFRESH_DIV 0xFF000210
#define REG_FFVM_DISP_BITBLT_ADDR 0xFF000214
#define REG_FFVM_DISP_BITBLT_XY   0xFF000218
#define REG_FFVM_DISP_BITBLT_WH   0xFF00021C

#define REG_FFVM_AUDIO_OUT_FMT    0xFF000300
#define REG_FFVM_AUDIO_OUT_ADDR   0xFF000304
#define REG_FFVM_AUDIO_OUT_HEAD   0xFF000308
#define REG_FFVM_AUDIO_OUT_TAIL   0xFF00030C
#define REG_FFVM_AUDIO_OUT_SIZE   0xFF000310
#define REG_FFVM_AUDIO_OUT_CURR   0xFF000314
#define REG_FFVM_AUDIO_OUT_LOCK   0xFF000318

#define REG_FFVM_AUDIO_IN_FMT     0xFF000320
#define REG_FFVM_AUDIO_IN_ADDR    0xFF000324
#define REG_FFVM_AUDIO_IN_HEAD    0xFF000328
#define REG_FFVM_AUDIO_IN_TAIL    0xFF00032C
#define REG_FFVM_AUDIO_IN_SIZE    0xFF000330
#define REG_FFVM_AUDIO_IN_CURR    0xFF000334
#define REG_FFVM_AUDIO_IN_LOCK    0xFF000338

#define REG_FFVM_MTIMECURL        0xFF000400
#define REG_FFVM_MTIMECURH        0xFF000404
#define REG_FFVM_MTIMECMPL        0xFF000408
#define REG_FFVM_MTIMECMPH        0xFF00040C
#define REG_FFVM_REALTIME         0xFF000410

#define REG_FFVM_DISK_SECTOR_NUM  0xFF000500
#define REG_FFVM_DISK_SECTOR_SIZE 0xFF000504
#define REG_FFVM_DISK_SECTOR_IDX  0xFF000508
#define REG_FFVM_DISK_SECTOR_DAT  0xFF00050C

#define REG_FFVM_CPU_FREQ         0xFF000600

typedef struct {
    uint32_t pc;
    uint32_t x[32];
    uint64_t f[32];
    uint32_t csr[0x1000];
    uint32_t mreserved;
    #define MAX_MEM_SIZE (64 * 1024 * 1024)
    uint8_t  mem[MAX_MEM_SIZE];

    #define FLAG_EXIT (1 << 0)
    uint32_t flags, freq;
    uint64_t ffvm_start_tick;
    uint32_t ffvm_realtime_diff;
    void    *adev, *vdev;
    IDEV    *idev;
    uint8_t *adev_out_buf;
    int      adev_out_len;
    pthread_mutex_t lock;

    uint32_t disp_wh;
    uint32_t disp_addr;
    uint32_t disp_refresh_xy;
    uint32_t disp_refresh_wh;
    uint32_t disp_refresh_div;
    uint32_t disp_bitblt_addr;
    uint32_t disp_bitblt_xy;
    uint32_t disp_bitblt_wh;
    uint32_t disp_refresh_cnt;

    uint32_t audio_out_fmt;
    uint32_t audio_out_addr;
    uint32_t audio_out_head;
    uint32_t audio_out_tail;
    uint32_t audio_out_size;
    uint32_t audio_out_curr;
    uint32_t audio_out_lock;

    uint32_t audio_in_fmt;
    uint32_t audio_in_addr;
    uint32_t audio_in_head;
    uint32_t audio_in_tail;
    uint32_t audio_in_size;
    uint32_t audio_in_curr;
    uint32_t audio_in_lock;

    uint64_t mtimecur;
    uint64_t mtimecmp;

    FILE    *disk_fp;
} RISCV;

static int ringbuf_write(uint8_t *rbuf, int maxsize, int tail, uint8_t *src, int len)
{
    uint8_t *buf1 = rbuf    + tail;
    int      len1 = maxsize - tail < len ? maxsize - tail : len;
    uint8_t *buf2 = rbuf;
    int      len2 = len - len1;
    memcpy(buf1, src + 0   , len1);
    memcpy(buf2, src + len1, len2);
    return len2 ? len2 : tail + len1;
}

static int ringbuf_read(uint8_t *rbuf, int maxsize, int head, uint8_t *dst, int len)
{
    uint8_t *buf1 = rbuf    + head;
    int      len1 = maxsize - head < len ? maxsize - head : len;
    uint8_t *buf2 = rbuf;
    int      len2 = len - len1;
    if (dst) {
        memcpy(dst + 0   , buf1, len1);
        memcpy(dst + len1, buf2, len2);
    }
    return len2 ? len2 : head + len1;
}

static uint64_t get_file_size(FILE *fp)
{
    uint64_t size;
    uint64_t off;
    off  = ftello(fp);
    fseeko(fp, 0  , SEEK_END);
    size = ftello(fp);
    fseeko(fp, off, SEEK_SET);
    return size;
}

static void disp_init(RISCV *riscv, int wh)
{
    if (riscv->disp_wh != wh) {
        riscv->disp_wh  = wh;
        vdev_exit(riscv->vdev, 1); riscv->vdev = riscv->idev = NULL;
        if (wh) {
            riscv->vdev = vdev_init((wh >> 0) & 0xFFFF, (wh >> 16) & 0xFFFF, NULL, NULL, NULL);
            riscv->idev = (void*)vdev_get(riscv->vdev, "idev", NULL);
        }
    }
}

static void disp_refresh(RISCV *riscv, uint32_t counter)
{
    int refresh = 0, rx, ry, dw, rw, rh, i;
    if (riscv->disp_refresh_div == 0 && riscv->disp_refresh_wh) refresh = 1;
    if (riscv->disp_refresh_div) {
        if (++riscv->disp_refresh_cnt >= riscv->disp_refresh_div) riscv->disp_refresh_cnt = 0, refresh = 1;
    }
    if (refresh) {
        dw = (riscv->disp_wh         >> 0) & 0xFFFF;
        rx = (riscv->disp_refresh_xy >> 0) & 0xFFFF;
        ry = (riscv->disp_refresh_xy >>16) & 0xFFFF;
        rw = (riscv->disp_refresh_wh >> 0) & 0xFFFF;
        rh = (riscv->disp_refresh_wh >>16) & 0xFFFF;
        BMP *bmp = vdev_lock(riscv->vdev);
        if (bmp) {
            uint32_t *src = (uint32_t*)(riscv->mem + riscv->disp_addr % MAX_MEM_SIZE) + ry * dw + rx;
            uint32_t *dst = (uint32_t*)bmp->pdata + ry * dw + rx;
            for (i = 0; i < rh; i++) {
                memcpy(dst, src, rw * sizeof(uint32_t));
                src += dw, dst += dw;
            }
            vdev_unlock(riscv->vdev);
        }
        if (riscv->disp_refresh_div == 0) riscv->disp_refresh_wh = 0;
    }
    if (counter % RISCV_FRAMERATE == 0) {
        char *state = (char*)vdev_get(riscv->vdev, "state", NULL);
        if (state && strcmp(state, "closed") == 0) riscv->disp_wh = 0;
    }
}

static void disp_bitblt(RISCV *riscv)
{
    int       dx  = (riscv->disp_bitblt_xy >> 0 ) & 0xFFFF;
    int       dy  = (riscv->disp_bitblt_xy >> 16) & 0xFFFF;
    int       dw  = (riscv->disp_wh >> 0) & 0xFFFF;
    int       sw  = (riscv->disp_bitblt_wh >> 0 ) & 0xFFFF;
    int       sh  = (riscv->disp_bitblt_wh >> 16) & 0xFFFF;
    uint32_t *src = (uint32_t*)(riscv->mem + riscv->disp_bitblt_addr % MAX_MEM_SIZE);
    uint32_t *dst = (uint32_t*)(riscv->mem + riscv->disp_addr % MAX_MEM_SIZE) + dy * dw + dx;
    for (int i = 0; i < sh; i++) {
        memcpy(dst, src, sw * sizeof(uint32_t));
        dst += dw, src += sw;
    }
}

static void ffvm_adev_callback(void *ctxt, int cmd, void *buf, int len)
{
    RISCV *riscv = ctxt;
    switch (cmd) {
    case ADEV_CMD_DATA_RECORD:
        pthread_mutex_lock(&riscv->lock);
        if (riscv->audio_in_size) {
            uint8_t *rbuf = &(riscv->mem[riscv->audio_in_addr % MAX_MEM_SIZE]);
            int      head = riscv->audio_in_head;
            int      tail = riscv->audio_in_tail;
            int      size = riscv->audio_in_size;
            int      curr = riscv->audio_in_curr;
            int      avail= size - curr;
            int      drop = len  - avail;
            if (drop > 0) {
                head  = ringbuf_read(rbuf, size, head, NULL, drop);
                curr -= drop;
            }
            tail  = ringbuf_write(rbuf, size, tail, buf, len);
            curr += len;
            riscv->audio_in_head = head;
            riscv->audio_in_tail = tail;
            riscv->audio_in_curr = curr;
        }
        pthread_mutex_unlock(&riscv->lock);
        break;
    }
}

static void audio_init(RISCV *riscv, uint32_t fmt, int flag)
{
    int new_ch   = fmt >> 24;
    int new_rate = fmt & 0xFFFFFF;
    int cur_ch, cur_rate, out_changed = 0, in_changed = 0;
    if (flag) { // audio in
        cur_ch   = riscv->audio_in_fmt  >> 24;
        cur_rate = riscv->audio_in_fmt  & 0xFFFFFF;
        riscv->audio_in_fmt  = fmt;
        if (cur_ch != new_ch || cur_rate != new_rate) in_changed  = 1;
    } else {    // audio out
        cur_ch   = riscv->audio_out_fmt >> 24;
        cur_rate = riscv->audio_out_fmt & 0xFFFFFF;
        riscv->audio_out_fmt = fmt;
        if (cur_ch != new_ch || cur_rate != new_rate) out_changed = 1;
    }
    if (out_changed) {
        free(riscv->adev_out_buf); riscv->adev_out_buf = NULL;
        adev_exit(riscv->adev);    riscv->adev         = NULL;
        if (riscv->audio_in_fmt || riscv->audio_out_fmt) {
            riscv->adev = adev_init(new_rate, new_ch, new_rate / 50, FFVM_ADEV_MAX_BUFNUM);
            riscv->adev_out_len = new_rate / 50 * sizeof(int16_t) * new_ch;
            riscv->adev_out_buf = malloc(riscv->adev_out_len);
        }
    }
    if (in_changed ) {
        if (!riscv->adev) {
            int out_ch   = riscv->audio_out_fmt >> 24;
            int out_rate = riscv->audio_out_fmt & 0xFFFFFF;
            riscv->adev = adev_init(out_rate, out_ch, out_rate / 50, FFVM_ADEV_MAX_BUFNUM);
            adev_set(riscv->adev, "callback", ffvm_adev_callback);
            adev_set(riscv->adev, "cbctx"   , riscv);
        }
        adev_record(riscv->adev, 0, 0, 0, 0, 0);
        if (riscv->audio_in_fmt) adev_record(riscv->adev, 1, new_rate, new_ch, new_rate / 50, FFVM_ADEV_MAX_BUFNUM);
    }
}

static void audio_update(RISCV *riscv, uint32_t counter)
{
    if (riscv->audio_out_lock || !riscv->audio_out_size) return;
     while (adev_get(riscv->adev, "bufnum", NULL) < FFVM_ADEV_MAX_BUFNUM) {
        uint8_t *rbuf = &(riscv->mem[riscv->audio_out_addr % MAX_MEM_SIZE]);
        int      head = riscv->audio_out_head;
        int      size = riscv->audio_out_size;
        int      curr = riscv->audio_out_curr;
        int      n    = curr < riscv->adev_out_len ? curr : riscv->adev_out_len;
        if (n > 0) {
            head  = ringbuf_read(rbuf, size, head, riscv->adev_out_buf, n);
            curr -= n;
            riscv->audio_out_head = head;
            riscv->audio_out_curr = curr;
            adev_play(riscv->adev, riscv->adev_out_buf, n, 0);
        }
    }
}

#define RISCV_CSR_MSTATUS         0x300
#define RISCV_CSR_MISA            0x301
#define RISCV_CSR_MIE             0x304
#define RISCV_CSR_MTVEC           0x305
#define RISCV_CSR_MSCRATCH        0x340
#define RISCV_CSR_MEPC            0x341
#define RISCV_CSR_MCAUSE          0x342
#define RISCV_CSR_MTVAL           0x343
#define RISCV_CSR_MIP             0x344

#define INTR_MACHINE_SOFTWARE     3
#define INTR_MACHINE_TIMER        7
#define INTR_MACHINE_EXTERNAL     11

static void riscv_interrupt(RISCV *riscv, int source)
{
    if (!(riscv->csr[RISCV_CSR_MSTATUS] & (1 << 3)) ) return; // if mstatus:mie disabled
    if (!(riscv->csr[RISCV_CSR_MIE] & (1 << source))) return; // if mie:source disabled

    //+ update mstatus:mpie, mstatus:mpie = mstatus:mie
    riscv->csr[RISCV_CSR_MSTATUS] &= ~(1 << 7);
    riscv->csr[RISCV_CSR_MSTATUS] |= (riscv->csr[RISCV_CSR_MSTATUS] & (1 << 3)) << 4;
    //- update mstatus:mpie, mstatus:mpie = mstatus:mie

    riscv->csr[RISCV_CSR_MSTATUS] &= ~(1 << 3 ); // update mstatus:mie to 0
    riscv->csr[RISCV_CSR_MIP] |= (1 << source);  // update mip:source to source

    // update mcause
    riscv->csr[RISCV_CSR_MCAUSE] = (1 << 31) | source; // interrupt and source

    uint32_t isr = riscv->csr[RISCV_CSR_MTVEC] & ~0x3;
    int      mode= riscv->csr[RISCV_CSR_MTVEC] &  0x3;
    if (mode == 1 && (riscv->csr[RISCV_CSR_MCAUSE] & (1 << 31))) isr += 4 * (riscv->csr[RISCV_CSR_MCAUSE] & ~(1 << 31));
    riscv->csr[RISCV_CSR_MEPC] = riscv->pc; // update mepc
    riscv->pc = isr;
}

static uint8_t riscv_memr8(RISCV *riscv, uint32_t addr)
{
    return *(riscv->mem + (addr & (MAX_MEM_SIZE - 1)));
}

static void riscv_memw8(RISCV *riscv, uint32_t addr, uint8_t data)
{
    *(riscv->mem + (addr & (MAX_MEM_SIZE - 1))) = data;
}

static uint16_t riscv_memr16(RISCV *riscv, uint32_t addr)
{
    if ((addr & 0x1) == 0) {
        return *(uint16_t*)(riscv->mem + (addr & (MAX_MEM_SIZE - 1)));
    } else {
        return (riscv->mem[(addr + 0) & (MAX_MEM_SIZE - 1)] << 0)
             | (riscv->mem[(addr + 1) & (MAX_MEM_SIZE - 1)] << 8);
    }
}

static void riscv_memw16(RISCV *riscv, uint32_t addr, uint16_t data)
{
    if ((addr & 0x1) == 0) {
        *(uint16_t*)(riscv->mem + (addr & (MAX_MEM_SIZE - 1))) = data;
    } else {
        riscv->mem[(addr + 0) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >> 0);
        riscv->mem[(addr + 1) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >> 8);
    }
}

static uint32_t riscv_memr32(RISCV *riscv, uint32_t addr)
{
    if (addr < REG_FFVM_STDIO) {
        if ((addr & 0x3) == 0) {
            return *(uint32_t*)(riscv->mem + (addr & (MAX_MEM_SIZE - 1)));
        } else {
            return (riscv->mem[(addr + 0) & (MAX_MEM_SIZE - 1)] << 0)
                 | (riscv->mem[(addr + 1) & (MAX_MEM_SIZE - 1)] << 8)
                 | (riscv->mem[(addr + 2) & (MAX_MEM_SIZE - 1)] <<16)
                 | (riscv->mem[(addr + 3) & (MAX_MEM_SIZE - 1)] <<24);
        }
    }

    if (addr == REG_FFVM_MTIMECURL || addr == REG_FFVM_MTIMECURH) riscv->mtimecur = get_tick_count() - riscv->ffvm_start_tick;
    switch (addr) {
    case REG_FFVM_STDIO    : return console_getc ();
    case REG_FFVM_GETCH    : return console_getch();
    case REG_FFVM_KBHIT    : return console_kbhit();
    case REG_FFVM_REALTIME : return time(NULL) - riscv->ffvm_realtime_diff;
    case REG_FFVM_MTIMECURL: return riscv->mtimecur >>  0;
    case REG_FFVM_MTIMECURH: return riscv->mtimecur >> 32;
    case REG_FFVM_MTIMECMPL: return riscv->mtimecmp >>  0;
    case REG_FFVM_MTIMECMPH: return riscv->mtimecmp >> 32;
    case REG_FFVM_MOUSE_XY : return (riscv->idev->mouse_x << 0) | (riscv->idev->mouse_y << 16);
    case REG_FFVM_MOUSE_BTN: return (riscv->idev->mouse_btns);
    case REG_FFVM_DISK_SECTOR_NUM : return get_file_size(riscv->disk_fp) / RISCV_DISK_SECTSIZE;
    case REG_FFVM_DISK_SECTOR_SIZE: return RISCV_DISK_SECTSIZE;
    case REG_FFVM_DISK_SECTOR_DAT : return fgetc(riscv->disk_fp);
    case REG_FFVM_CPU_FREQ : return riscv->freq;
    }
    if (addr >= REG_FFVM_KEYBD1 && addr <= REG_FFVM_KEYBD4) { return *(riscv->idev->key_bits + (addr - REG_FFVM_KEYBD1) / sizeof(uint32_t)); }
    if (addr >= REG_FFVM_DISP_WH && addr <= REG_FFVM_DISP_BITBLT_WH) return *(&riscv->disp_wh + (addr - REG_FFVM_DISP_WH) / sizeof(uint32_t));
    if (addr >= REG_FFVM_AUDIO_OUT_FMT && addr <= REG_FFVM_AUDIO_OUT_LOCK) return *(&riscv->audio_out_fmt + (addr - REG_FFVM_AUDIO_OUT_FMT) / sizeof(uint32_t));
    if (addr >= REG_FFVM_AUDIO_IN_FMT  && addr <= REG_FFVM_AUDIO_IN_LOCK ) return *(&riscv->audio_in_fmt  + (addr - REG_FFVM_AUDIO_IN_FMT ) / sizeof(uint32_t));
    return 0;
}

static void riscv_memw32(RISCV *riscv, uint32_t addr, uint32_t data)
{
    if (addr < REG_FFVM_STDIO) {
        if ((addr & 0x3) == 0) {
            *(uint32_t*)(riscv->mem + (addr & (MAX_MEM_SIZE - 1))) = data;
        } else {
            riscv->mem[(addr + 0) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >> 0);
            riscv->mem[(addr + 1) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >> 8);
            riscv->mem[(addr + 2) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >>16);
            riscv->mem[(addr + 3) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >>24);
        }
        return;
    }

    switch (addr) {
    case REG_FFVM_STDIO  : if (data == (uint32_t)-1) fflush(stdout); else fputc(data, stdout); return;
    case REG_FFVM_STDERR : if (data == (uint32_t)-1) fflush(stderr); else fputc(data, stderr); return;
    case REG_FFVM_CLRSCR : console_clrscr(); return;
    case REG_FFVM_GOTOXY : console_gotoxy((data >> 0) & 0xFFFF, (data >> 16) & 0xFFFF); return;
    case REG_FFVM_DISP_WH: disp_init(riscv, data); break;
    case REG_FFVM_AUDIO_OUT_FMT: audio_init(riscv, data, 0); break;
    case REG_FFVM_AUDIO_IN_FMT : audio_init(riscv, data, 1); break;
    case REG_FFVM_AUDIO_IN_LOCK:
        if (data) pthread_mutex_lock(&riscv->lock);
        else    pthread_mutex_unlock(&riscv->lock);
        break;
    case REG_FFVM_REALTIME : riscv->ffvm_realtime_diff = time(NULL) - data; return;
    case REG_FFVM_MTIMECMPL: ((uint32_t*)&riscv->mtimecmp)[0] = data; return;
    case REG_FFVM_MTIMECMPH: ((uint32_t*)&riscv->mtimecmp)[1] = data; return;
    case REG_FFVM_DISK_SECTOR_IDX: fseeko(riscv->disk_fp, data * RISCV_DISK_SECTSIZE, SEEK_SET); return;
    case REG_FFVM_DISK_SECTOR_DAT: fputc(data, riscv->disk_fp); return;
    case REG_FFVM_CPU_FREQ: riscv->freq = data < RISCV_CPU_FREQ_MAX ? data : RISCV_CPU_FREQ_MAX; return;
    }
    if (addr >= REG_FFVM_DISP_ADDR && addr <= REG_FFVM_DISP_BITBLT_WH) {
        *(&riscv->disp_addr + (addr - REG_FFVM_DISP_ADDR) / sizeof(uint32_t)) = data;
        if (addr == REG_FFVM_DISP_BITBLT_WH && riscv->disp_bitblt_wh) disp_bitblt(riscv);
    }
    else if (addr >= REG_FFVM_AUDIO_OUT_ADDR && addr <= REG_FFVM_AUDIO_OUT_LOCK) *(&riscv->audio_out_addr + (addr - REG_FFVM_AUDIO_OUT_ADDR) / sizeof(uint32_t)) = data;
    else if (addr >= REG_FFVM_AUDIO_IN_ADDR  && addr <= REG_FFVM_AUDIO_IN_LOCK ) *(&riscv->audio_in_addr  + (addr - REG_FFVM_AUDIO_IN_ADDR ) / sizeof(uint32_t)) = data;
}

static int32_t signed_extend(uint32_t a, int size)
{
    return (a & (1 << (size - 1))) ? (a | ~((1 << size) - 1)) : a;
}

static uint32_t handle_ecall(RISCV *riscv)
{
    switch (riscv->x[17]) {
    case 93: riscv->flags |= FLAG_EXIT; return 0; // sys_exit
    default: return 0;
    }
}

static void riscv_execute_rv16(RISCV *riscv, uint16_t instruction)
{
    const uint16_t inst_opcode = (instruction >> 0) & 0x3;
    const uint16_t inst_rd     = (instruction >> 7) & 0x1f;
    const uint16_t inst_rs1    = (instruction >> 7) & 0x1f;
    const uint16_t inst_rs2    = (instruction >> 2) & 0x1f;
    const uint16_t inst_rs1s   = (instruction >> 7) & 0x7;
    const uint16_t inst_rs2s   = (instruction >> 2) & 0x7;
    const uint16_t inst_rds    = (instruction >> 2) & 0x7;
    const uint16_t inst_imm6   =((instruction >> 2) & 0x1f) | ((instruction >> 7) & (1 << 5));
    const uint16_t inst_imm7   =((instruction >> 4) & (1 << 2)) | ((instruction >> 7) & (0x7 << 3)) | ((instruction << 1) & (1 << 6));
    const uint16_t inst_imm8   =((instruction >> 7) & (0x7 << 3)) | ((instruction << 1) & (0x3 << 6));
    const uint16_t inst_imm9   =((instruction >> 2) & (0x3 << 3)) | ((instruction >> 7) & (1 << 5)) | ((instruction << 4) & (0x7 << 6));
    const uint16_t inst_imm10  =((instruction >> 4) & (1 << 2)) | ((instruction >> 2) & (1 << 3)) | ((instruction >> 7) & (0x3 << 4)) | ((instruction >> 1) & (0x7 << 6));
    const uint16_t inst_imm12  =((instruction >> 2) & (0x7 << 1)) | ((instruction >> 7) & (1 << 4)) | ((instruction << 3) & (1 << 5))
                               |((instruction >> 1) & (0x2d << 6)) | ((instruction << 1) & (1 << 7)) | ((instruction << 2) & (1 << 10));
    const uint32_t inst_imm18  =((instruction << 5) & (1 << 17)) | ((instruction << 10) & (0x1f << 12));
    const uint16_t inst_funct2 = (instruction >>10) & 0x3;
    const uint16_t inst_funct3 = (instruction >>13) & 0x7;
    uint32_t bflag = 0, temp;

    switch (inst_opcode) {
    case 0:
        switch (inst_funct3) {
        case 0: riscv->x[8 + inst_rds] = riscv->x[2] + inst_imm10; break; // c.addi4spn
        case 1: // c.fld
            riscv->f[8 + inst_rds] = (uint64_t)riscv_memr32(riscv, riscv->x[8 + inst_rs1s] + inst_imm8 + 0) << 0 ;
            riscv->f[8 + inst_rds]|= (uint64_t)riscv_memr32(riscv, riscv->x[8 + inst_rs1s] + inst_imm8 + 4) << 32;
            break;
        case 2: riscv->x[8 + inst_rds] = riscv_memr32(riscv, riscv->x[8 + inst_rs1s] + inst_imm7); break; // c.lw
        case 3: riscv->f[8 + inst_rds] = riscv_memr32(riscv, riscv->x[8 + inst_rs1s] + inst_imm7); break; // c.flw
        case 5: // c.fsd
            riscv_memw32(riscv, riscv->x[8 + inst_rs1s] + inst_imm8 + 0, (uint32_t)(riscv->f[8 + inst_rs2s] >> 0 ));
            riscv_memw32(riscv, riscv->x[8 + inst_rs1s] + inst_imm8 + 4, (uint32_t)(riscv->f[8 + inst_rs2s] >> 32));
            break;
        case 6: riscv_memw32(riscv, riscv->x[8 + inst_rs1s] + inst_imm7, riscv->x[8 + inst_rs2s]); break; // c.sw
        case 7: riscv_memw32(riscv, riscv->x[8 + inst_rs1s] + inst_imm7, (uint32_t)riscv->f[8 + inst_rs2s]); break; // c.fsw
        }
        break;
    case 1:
        switch (inst_funct3) {
        case 0: riscv->x[inst_rd] += signed_extend(inst_imm6, 6); break; // c.addi
        case 1: // c.jal
            riscv->x[1] = riscv->pc + 2;
            riscv->pc  += signed_extend(inst_imm12, 12);
            bflag = 1;
            break;
        case 2: riscv->x[inst_rd] = signed_extend(inst_imm6, 6); break; // c.li
        case 3:
            if (inst_rd == 2) { // c.addi16sp
                temp = ((instruction >> 2) & (1 << 4)) | ((instruction << 3) & (1 << 5)) | ((instruction << 1) & (1 << 6))
                     | ((instruction << 4) & (0x3 << 7)) | ((instruction >> 3) & (1 << 9));
                riscv->x[inst_rd] += signed_extend(temp, 10);
            } else { // c.lui
                riscv->x[inst_rd]  = signed_extend(inst_imm18, 18);
            }
            break;
        case 4:
            switch (inst_funct2) {
            case 0: riscv->x[8 + inst_rs1s] = (uint32_t)riscv->x[8 + inst_rs1s] >> inst_imm6; break; // c.srli
            case 1: riscv->x[8 + inst_rs1s] = (int32_t )riscv->x[8 + inst_rs1s] >> inst_imm6; break; // c.srai
            case 2: riscv->x[8 + inst_rs1s]&= signed_extend(inst_imm6, 6); break; // c.andi
            case 3:
                switch ((instruction >> 5) & 3) {
                case 0: riscv->x[8 + inst_rs1s] -= riscv->x[8 + inst_rs2s]; break; // c.sub
                case 1: riscv->x[8 + inst_rs1s] ^= riscv->x[8 + inst_rs2s]; break; // c.xor
                case 2: riscv->x[8 + inst_rs1s] |= riscv->x[8 + inst_rs2s]; break; // c.or
                case 3: riscv->x[8 + inst_rs1s] &= riscv->x[8 + inst_rs2s]; break; // c.and
                }
                break;
            }
            break;
        case 5: riscv->pc += signed_extend(inst_imm12, 12); bflag = 1; break; // c.j
        case 6: // c.beqz
        case 7: // c.bnez
            if ((inst_funct3 == 6 && riscv->x[8 + inst_rs1s] == 0) || (inst_funct3 == 7 && riscv->x[8 + inst_rs1s] != 0)) {
                temp = ((instruction >> 2) & (0x3 << 1)) | ((instruction >> 7) & (0x3 << 3)) | ((instruction << 3) & (1 << 5))
                     | ((instruction << 1) & (0x3 << 6)) | ((instruction >> 4) & (1 << 8));
                riscv->pc += signed_extend(temp, 9);
                bflag = 1;
            }
            break;
        }
        break;
    case 2:
        switch (inst_funct3) {
        case 0: riscv->x[inst_rd] <<= inst_imm6; break; // c.slli
        case 1: // c.fldsp
            riscv->f[inst_rd]  = (uint64_t)riscv_memr32(riscv, riscv->x[2] + inst_imm9 + 0) << 0 ;
            riscv->f[inst_rd] |= (uint64_t)riscv_memr32(riscv, riscv->x[2] + inst_imm9 + 4) << 32;
            break;
        case 2: // c.lwsp
        case 3: // c.flwsp
            temp = ((instruction >> 2) & (0x7 << 2)) | ((instruction >> 7) & (1 << 5)) | ((instruction << 4) & (0x3 << 6));
            if (inst_funct3 == 2) riscv->x[inst_rd] = riscv_memr32(riscv, riscv->x[2] + temp); // c.lwsp
            else                  riscv->f[inst_rd] = riscv_memr32(riscv, riscv->x[2] + temp); // c.flwsp
            break;
        case 4:
            if ((instruction & (1 << 12)) == 0) {
                if (inst_rs2 == 0) { // c.jr
                    riscv->pc = riscv->x[inst_rs1]; bflag = 1;
                } else { // c.mv
                    riscv->x[inst_rd] = riscv->x[inst_rs2];
                }
            } else {
                if (inst_rs1 == 0 && inst_rs2 == 0) { // c.ebreak;
                } else if (inst_rs2 == 0) { // c.jalr
                    temp        = riscv->pc + 2;
                    riscv->pc   = riscv->x[inst_rs1];
                    riscv->x[1] = temp;
                    bflag       = 1;
                } else { // c.add
                    riscv->x[inst_rd] += riscv->x[inst_rs2];
                }
            }
            break;
        case 5: // c.fsdsp
            temp = ((instruction >> 7) & (0x7 << 3)) | ((instruction >> 1) & (0x7 << 6));
            riscv_memw32(riscv, riscv->x[2] + temp + 0, (uint32_t)(riscv->f[inst_rs2] >> 0 ));
            riscv_memw32(riscv, riscv->x[2] + temp + 4, (uint32_t)(riscv->f[inst_rs2] >> 32));
            break;
        case 6: // c.swsp
        case 7: // c.fswsp
            temp = ((instruction >> 7) & (0xf << 2)) | ((instruction >> 1) & (0x3 << 6));
            riscv_memw32(riscv, riscv->x[2] + temp, inst_funct3 == 6 ? riscv->x[inst_rs2] : (uint32_t)riscv->f[inst_rs2]);
            break;
        }
        break;
    }
    riscv->pc += bflag ? 0 : 2;
}

static void riscv_execute_rv32(RISCV *riscv, uint32_t instruction)
{
    const uint32_t inst_opcode = (instruction >>  0) & 0x7f;
    const uint32_t inst_rd     = (instruction >>  7) & 0x1f;
    const uint32_t inst_funct3 = (instruction >> 12) & 0x07;
    const uint32_t inst_rs1    = (instruction >> 15) & 0x1f;
    const uint32_t inst_rs2    = (instruction >> 20) & 0x1f;
    const uint32_t inst_funct7 = (instruction >> 25) & 0x7f;
    const uint32_t inst_imm12i = (instruction >> 20);
    const uint32_t inst_imm12s =((instruction >> 20) & (0x7f << 5)) | ((instruction >> 7) & 0x1f);
    const uint32_t inst_imm13b =((instruction >> 19) & (0x1  <<12)) | ((instruction << 4) & (0x1 << 11))
                               |((instruction >> 20) & (0x3f << 5)) | ((instruction >> 7) & (0xf <<  1));
    const uint32_t inst_imm20u = instruction & (0xfffff << 12);
    const uint32_t inst_imm21j =((instruction >> 11) & (1 << 20)) | (instruction & (0xff << 12))
                               |((instruction >> 9 ) & (1 << 11)) | ((instruction >> 20) & (0x3ff << 1));
    const uint32_t inst_csr    = (instruction >> 20);
    uint32_t bflag = 0, maddr, temp;
    int64_t  mult64res;

    switch (inst_opcode) {
    case 0x37: riscv->x[inst_rd] = inst_imm20u; break; // u-type lui
    case 0x17: riscv->x[inst_rd] = riscv->pc + (int32_t)inst_imm20u; break; // u-type auipc
    case 0x6f: // j-type jal
        riscv->x[inst_rd] = riscv->pc + 4;
        riscv->pc += signed_extend(inst_imm21j, 21);
        bflag = 1;
        break;
    case 0x67: // i-type
        switch (inst_funct3) {
        case 0x0: // jalr
            temp = riscv->pc + 4;
            riscv->pc = riscv->x[inst_rs1] + signed_extend(inst_imm12i, 12);
            riscv->pc&=~(1 << 0);
            riscv->x[inst_rd] = temp;
            bflag = 1;
            break;
        }
        break;
    case 0x63: // b-type
        switch (inst_funct3) {
        case 0x0: bflag = riscv->x[inst_rs1] == riscv->x[inst_rs2]; break; // beq
        case 0x1: bflag = riscv->x[inst_rs1] != riscv->x[inst_rs2]; break; // bne
        case 0x4: bflag = (int32_t)riscv->x[inst_rs1] <  (int32_t)riscv->x[inst_rs2]; break; // blt
        case 0x5: bflag = (int32_t)riscv->x[inst_rs1] >= (int32_t)riscv->x[inst_rs2]; break; // bge
        case 0x6: bflag = riscv->x[inst_rs1] <  riscv->x[inst_rs2]; break; // bltu
        case 0x7: bflag = riscv->x[inst_rs1] >= riscv->x[inst_rs2]; break; // bgeu
        }
        if (bflag) riscv->pc += signed_extend(inst_imm13b, 13);
        break;
    case 0x03: // i-type
        maddr = riscv->x[inst_rs1] + signed_extend(inst_imm12i, 12);
        switch (inst_funct3) {
        case 0x0: riscv->x[inst_rd] = (int8_t )riscv_memr8 (riscv, maddr); break; // lb
        case 0x1: riscv->x[inst_rd] = (int16_t)riscv_memr16(riscv, maddr); break; // lh
        case 0x2: riscv->x[inst_rd] = riscv_memr32(riscv, maddr); break; // lw
        case 0x4: riscv->x[inst_rd] = riscv_memr8 (riscv, maddr); break; // lbu
        case 0x5: riscv->x[inst_rd] = riscv_memr16(riscv, maddr); break; // lhu
        }
        break;
    case 0x23: // s-type
        maddr = riscv->x[inst_rs1] + signed_extend(inst_imm12s, 12);
        switch (inst_funct3) {
        case 0x0: riscv_memw8 (riscv, maddr, (uint8_t )riscv->x[inst_rs2]); break; // sb
        case 0x1: riscv_memw16(riscv, maddr, (uint16_t)riscv->x[inst_rs2]); break; // sh
        case 0x2: riscv_memw32(riscv, maddr, riscv->x[inst_rs2]); break; // sw
        }
        break;
    case 0x13: // i-type
        switch (inst_funct3) {
        case 0x0: riscv->x[inst_rd] = riscv->x[inst_rs1] + signed_extend(inst_imm12i, 12); break; // addi
        case 0x2: riscv->x[inst_rd] = (int32_t)riscv->x[inst_rs1] < signed_extend(inst_imm12i, 12);  break; // slti
        case 0x3: riscv->x[inst_rd] = riscv->x[inst_rs1] < (uint32_t)signed_extend(inst_imm12i, 12); break; // sltiu
        case 0x4: riscv->x[inst_rd] = riscv->x[inst_rs1] ^ (signed_extend(inst_imm12i, 12)); break; // xori
        case 0x6: riscv->x[inst_rd] = riscv->x[inst_rs1] | (signed_extend(inst_imm12i, 12)); break; // ori
        case 0x7: riscv->x[inst_rd] = riscv->x[inst_rs1] & (signed_extend(inst_imm12i, 12)); break; // andi
        case 0x1: riscv->x[inst_rd] = riscv->x[inst_rs1] << (inst_imm12i & 0x1f); break; // slli
        case 0x5: // srli & srai
            if (inst_funct7 & (1 << 5)) { // srai
                riscv->x[inst_rd] = (int32_t)riscv->x[inst_rs1] >> (inst_imm12i & 0x1f);
            } else { // srli
                riscv->x[inst_rd] = riscv->x[inst_rs1] >> (inst_imm12i & 0x1f);
            }
            break;
        }
        break;
    case 0x33: // r-type
        if ((inst_funct7 & (1 << 0)) == 0) {
            switch (inst_funct3) {
            case 0x0: // add & sub
                if (inst_funct7 & (1 << 5)) { // sub
                    riscv->x[inst_rd] = riscv->x[inst_rs1] - riscv->x[inst_rs2];
                } else { // add
                    riscv->x[inst_rd] = riscv->x[inst_rs1] + riscv->x[inst_rs2];
                }
                break;
            case 0x1: riscv->x[inst_rd] = riscv->x[inst_rs1] << (riscv->x[inst_rs2] & 0x1f); break; // sll
            case 0x2: riscv->x[inst_rd] = (int32_t)riscv->x[inst_rs1] < (int32_t)riscv->x[inst_rs2]; break; // slt
            case 0x3: riscv->x[inst_rd] = riscv->x[inst_rs1] < riscv->x[inst_rs2]; break; // sltu
            case 0x4: riscv->x[inst_rd] = riscv->x[inst_rs1] ^ riscv->x[inst_rs2]; break; // xor
            case 0x5: // srl & sra
                if (inst_funct7 & (1 << 5)) { // sra
                    riscv->x[inst_rd] = (int32_t)riscv->x[inst_rs1] >> (riscv->x[inst_rs2] & 0x1f);
                } else { // srl
                    riscv->x[inst_rd] = riscv->x[inst_rs1] >> (riscv->x[inst_rs2] & 0x1f);
                }
                break;
            case 0x6: riscv->x[inst_rd] = riscv->x[inst_rs1] | riscv->x[inst_rs2]; break; // or
            case 0x7: riscv->x[inst_rd] = riscv->x[inst_rs1] & riscv->x[inst_rs2]; break; // and
            }
        } else {
            switch (inst_funct3) {
            case 0x0: riscv->x[inst_rd] = riscv->x[inst_rs1] * riscv->x[inst_rs2]; break; // mul
            case 0x1: // mulh
                mult64res = (int64_t)riscv->x[inst_rs1] * (int64_t)riscv->x[inst_rs2];
                riscv->x[inst_rd] = (uint32_t)(mult64res >> 32);
                break;
            case 0x2: // mulhsu
                mult64res = (int64_t)riscv->x[inst_rs1] * (uint64_t)riscv->x[inst_rs2];
                riscv->x[inst_rd] = (uint32_t)(mult64res >> 32);
                break;
            case 0x3: // mulhu
                mult64res = (uint64_t)riscv->x[inst_rs1] * (uint64_t)riscv->x[inst_rs2];
                riscv->x[inst_rd] = (uint32_t)(mult64res >> 32);
                break;
            case 0x4: // div
                riscv->x[inst_rd] = (uint32_t)((int32_t)riscv->x[inst_rs1] / (int32_t)riscv->x[inst_rs2]);
                break;
            case 0x5: // divu
                riscv->x[inst_rd] = riscv->x[inst_rs1] / riscv->x[inst_rs2];
                break;
            case 0x6: // rem
                riscv->x[inst_rd] = (uint32_t)((int32_t)riscv->x[inst_rs1] % (int32_t)riscv->x[inst_rs2]);
                break;
            case 0x7: // remu
                riscv->x[inst_rd] = riscv->x[inst_rs1] % riscv->x[inst_rs2];
                break;
            }
        }
        break;
    case 0x73:
        switch (inst_funct3) {
        case 0:
            if (inst_csr == 0) { // ecall
                riscv->x[10] = handle_ecall(riscv);
            } else if (inst_csr == 1) { // ebreak
            } else if (inst_csr == 0x302) { // mret
                bflag = 1;
                riscv->pc = riscv->csr[RISCV_CSR_MEPC];
                //+ restore mstatus:mie, mstatus:mie = mstatus:mpie
                riscv->csr[RISCV_CSR_MSTATUS] &=~(1 << 3);
                riscv->csr[RISCV_CSR_MSTATUS] |= (riscv->csr[RISCV_CSR_MSTATUS] & (1 << 7)) >> 4;
                //- restore mstatus:mie, mstatus:mie = mstatus:mpie
                riscv->csr[RISCV_CSR_MSTATUS] |= (1 << 7);
            }
            break;
        case 1: riscv->x[inst_rd] = riscv->csr[inst_csr]; if ((inst_csr >> 10) != 3) riscv->csr[inst_csr] = riscv->x[inst_rs1]; break; // csrrw
        case 2: riscv->x[inst_rd] = riscv->csr[inst_csr]; if ((inst_csr >> 10) != 3 && inst_rs1) riscv->csr[inst_csr] |= riscv->x[inst_rs1]; break; // csrrs
        case 3: riscv->x[inst_rd] = riscv->csr[inst_csr]; if ((inst_csr >> 10) != 3 && inst_rs1) riscv->csr[inst_csr] &=~riscv->x[inst_rs1]; break; // csrrc
        case 5: riscv->x[inst_rd] = riscv->csr[inst_csr]; if ((inst_csr >> 10) != 3) riscv->csr[inst_csr] = inst_rs1;                        break; // csrrwi
        case 6: riscv->x[inst_rd] = riscv->csr[inst_csr]; if ((inst_csr >> 10) != 3 && inst_rs1) riscv->csr[inst_csr] |= inst_rs1; break; // csrrsi
        case 7: riscv->x[inst_rd] = riscv->csr[inst_csr]; if ((inst_csr >> 10) != 3 && inst_rs1) riscv->csr[inst_csr] &=~inst_rs1; break; // csrrci
        }
        break;
    case 0x2f:
        if (inst_funct3 == 0x2) {
            temp = riscv_memr32(riscv, riscv->x[inst_rs1]);
            switch (instruction >> 27) {
            case 0x02: riscv->mreserved = riscv->x[inst_rs1]; break; // lr.w
            case 0x03: // sc.w
                if (riscv->mreserved == riscv->x[inst_rs1]) riscv_memw32(riscv, riscv->x[inst_rs1], riscv->x[inst_rs2]);
                temp = !(riscv->mreserved == riscv->x[inst_rs1]);
                break;
            case 0x01: riscv_memw32(riscv, riscv->x[inst_rs1], riscv->x[inst_rs2]); break; // amoswap.w
            case 0x00: riscv_memw32(riscv, riscv->x[inst_rs1], temp + riscv->x[inst_rs2]); break; // amoadd.w
            case 0x04: riscv_memw32(riscv, riscv->x[inst_rs1], temp ^ riscv->x[inst_rs2]); break; // amoxor.w
            case 0x0c: riscv_memw32(riscv, riscv->x[inst_rs1], temp & riscv->x[inst_rs2]); break; // amoand.w
            case 0x08: riscv_memw32(riscv, riscv->x[inst_rs1], temp | riscv->x[inst_rs2]); break; // amoor.w
            case 0x10: riscv_memw32(riscv, riscv->x[inst_rs1], (int32_t)temp < (int32_t)riscv->x[inst_rs2] ? temp : riscv->x[inst_rs2]); break; // amomin.w
            case 0x14: riscv_memw32(riscv, riscv->x[inst_rs1], (int32_t)temp > (int32_t)riscv->x[inst_rs2] ? temp : riscv->x[inst_rs2]); break; // amomax.w
            case 0x18: riscv_memw32(riscv, riscv->x[inst_rs1], temp < riscv->x[inst_rs2] ? temp : riscv->x[inst_rs2]); break; // amominu.w
            case 0x1c: riscv_memw32(riscv, riscv->x[inst_rs1], temp > riscv->x[inst_rs2] ? temp : riscv->x[inst_rs2]); break; // amomaxu.w
            }
            riscv->x[inst_rd] = temp;
        }
        break;
    case 0x0f:
        if (instruction == 0x0000100f) { // fence.i
            // todo...
        } else if ((instruction & 0xf00fff80) == 0) { // fence
            // todo...
        }
        break;
    }
    riscv->pc += bflag ? 0 : 4;
}

void riscv_run(RISCV *riscv)
{
    const uint32_t instruction = riscv_memr32(riscv, riscv->pc);
    if ((instruction & 0x3) != 0x3) {
        riscv_execute_rv16(riscv, (uint16_t)instruction);
    } else {
        riscv_execute_rv32(riscv, (uint32_t)instruction);
    }
    riscv->x[0] = 0;
}

RISCV* riscv_init(char *rom, char *disk)
{
    FILE  *fp    = NULL;
    RISCV *riscv = calloc(1, sizeof(RISCV));
    if (!riscv) return NULL;
    riscv->csr[RISCV_CSR_MISA] = (1 << 8) | (1 << 12) | (1 << 0) | (1 << 2); // misa rv32imac
    riscv->ffvm_start_tick = get_tick_count();
    riscv->pc       = 0x80000000;
    riscv->mtimecmp = 0xFFFFFFFFFFFFFFFFull;
    riscv->freq     = RISCV_CPU_FREQ_MAX;
    fp = fopen(rom, "rb");
    if (fp) {
        fread(riscv->mem, 1, sizeof(riscv->mem), fp);
        fclose(fp);
    }
    riscv->disk_fp = fopen(disk, "rb+");
    pthread_mutex_init(&riscv->lock, NULL);
    return riscv;
}

void riscv_free(RISCV *riscv)
{
    if (!riscv) return;
    if (riscv->audio_in_lock) {
        riscv->audio_in_lock = 0;
        pthread_mutex_unlock(&riscv->lock);
    }
    vdev_exit(riscv->vdev, 1);
    adev_exit(riscv->adev);
    pthread_mutex_destroy(&riscv->lock);
    if (riscv->disk_fp) fclose(riscv->disk_fp);
    free(riscv->adev_out_buf);
    free(riscv);
}

int main(int argc, char *argv[])
{
    char *rom  = "test.rom";
    char *disk = "disk.img";
    uint32_t next_tick = 0, run_counter = 0;
    int32_t  sleep_tick, i, j;
    RISCV   *riscv = NULL;

    for (i = 1; i < argc; i++) {
        if (strstr(argv[i], "--disk=") == argv[i]) disk = argv[i] + sizeof("--disk=") - 1;
        else rom = argv[i];
    }

    printf("rom : %s\n", rom );
    printf("disk: %s\n", disk);

    console_init();
    if (!(riscv = riscv_init(rom, disk))) return 0;

    next_tick = (uint32_t)get_tick_count();
    while (!(riscv->flags & (FLAG_EXIT))) {
        for (j = 0; j < 2; j++) {
            for (i = 0; i < riscv->freq / RISCV_FRAMERATE / 2; i++) riscv_run(riscv);
            riscv->mtimecur = get_tick_count() - riscv->ffvm_start_tick;
            if (riscv->mtimecur >= riscv->mtimecmp) riscv_interrupt(riscv, INTR_MACHINE_TIMER);
        }
        disp_refresh(riscv, run_counter  );
        audio_update(riscv, run_counter++);

        next_tick += 1000 / RISCV_FRAMERATE;
        sleep_tick = (int32_t)next_tick - (int32_t)get_tick_count();
        if (riscv->audio_in_lock) pthread_mutex_unlock(&riscv->lock);
        if (sleep_tick > 0) usleep(sleep_tick * 1000);
        if (riscv->audio_in_lock) pthread_mutex_lock  (&riscv->lock);
//      printf("sleep_tick: %d\n", sleep_tick);
    }

    riscv_free(riscv);
    console_exit();
    return 0;
}
