#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ffvm.h"

typedef struct {
    #define PS_C (1 << 0)
    #define PS_Z (1 << 1)
    #define PS_N (1 << 2)
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eps;
    uint32_t epc;
    uint8_t *mem_buf ;
    uint32_t mem_size;
} FFVM;

static uint8_t VM_MEM_READ8(FFVM *vm, uint32_t addr)
{
    return addr < vm->mem_size ? vm->mem_buf[addr] : 0xff;
}

static void VM_MEM_WRITE8(FFVM *vm, uint32_t addr, uint8_t val)
{
    if (addr < vm->mem_size) vm->mem_buf[addr] = val;
}

static uint16_t VM_MEM_READ16(FFVM *vm, uint32_t addr)
{
    return addr < vm->mem_size ? *(uint16_t*)&vm->mem_buf[addr] : 0;
}

static void VM_MEM_WRITE16(FFVM *vm, uint32_t addr, uint16_t val)
{
    if (addr < vm->mem_size) *(uint16_t*)&vm->mem_buf[addr] = val;
}

static uint32_t VM_MEM_READ32(FFVM *vm, uint32_t addr)
{
    return addr < vm->mem_size ? *(uint32_t*)&vm->mem_buf[addr] : 0;
}

static void VM_MEM_WRITE32(FFVM *vm, uint32_t addr, uint32_t val)
{
    if (addr < vm->mem_size) *(uint32_t*)&vm->mem_buf[addr] = val;
}

static void ffmv_dump(void *ctx)
{
    FFVM *vm = (FFVM*)ctx;
    if (vm) {
        printf("eax      ebx      ecx      edx      ebp      esp      eps      epc\n");
        printf("%08x %08x %08x %08x %08x %08x %08x %08x\n"  , vm->eax, vm->ebx, vm->ecx, vm->edx, vm->ebp, vm->esp, vm->eps, vm->epc);
        printf("[0x8000] [0x8004] [0x8008] [0x800C] [0x8010] [0x8014] [0x8018] [0x801C]\n");
        printf("%08x %08x %08x %08x %08x %08x %08x %08x\n\n",
               *(uint32_t*)&vm->mem_buf[0x8000], *(uint32_t*)&vm->mem_buf[0x8004], *(uint32_t*)&vm->mem_buf[0x8008], *(uint32_t*)&vm->mem_buf[0x800C],
               *(uint32_t*)&vm->mem_buf[0x8010], *(uint32_t*)&vm->mem_buf[0x8014], *(uint32_t*)&vm->mem_buf[0x8018], *(uint32_t*)&vm->mem_buf[0x801C]);
    }
}

void* ffvm_init(int memsize)
{
    FFVM *vm = malloc(sizeof(FFVM));
    if (!vm) return NULL;
    vm->mem_buf = malloc(memsize + 4);
    vm->mem_size= vm->mem_buf ? memsize : 0;
    ffvm_reset(vm);
    return vm;
}

void ffvm_exit(void *ctx)
{
    FFVM *vm = (FFVM*)ctx;
    if (vm) {
        if (vm->mem_buf) {
            free(vm->mem_buf);
        }
        free(vm);
    }
}

void ffvm_reset(void *ctx)
{
    FFVM *vm = (FFVM*)ctx;
    if (vm) vm->eax = vm->ebx = vm->ecx = vm->edx = vm->ebp = vm->esp = vm->epc = vm->eps = 0;
}

static void* fetch_opnd_ptr(FFVM *vm, uint8_t type, uint8_t *size)
{
    if (type < 7) {
        *size = 4;
        return &vm->eax + type;
    } else {
        switch (type) {
        case 7 : *size = 2; return (uint16_t*)&vm->eax;
        case 8 : *size = 2; return (uint16_t*)&vm->ebx;
        case 9 : *size = 1; return (uint8_t *)&vm->eax;
        case 10: *size = 1; return (uint8_t *)&vm->eax + 1;
        case 11: *size = 1; return (uint8_t *)&vm->ebx;
        case 12: *size = 1; return (uint8_t *)&vm->ebx + 1;
        case 13: *size = 1; break;
        case 14: *size = 2; break;
        case 15: *size = 4; break;
        }
    }
    return NULL;
}

static uint32_t fetch_opnd_val(FFVM *vm, uint8_t type, uint8_t *size)
{
    uint32_t val;
    if (type < 7) {
        *size = 4;
        return *(&vm->eax + type);
    } else {
        switch (type) {
        case 7 : *size = 2; return  vm->eax & 0xffff;
        case 8 : *size = 2; return  vm->ebx & 0xffff;
        case 9 : *size = 1; return  vm->eax & 0x00ff;
        case 10: *size = 1; return (vm->eax >> 8) & 0x00ff;
        case 11: *size = 1; return  vm->ebx & 0x00ff;
        case 12: *size = 1; return (vm->ebx >> 8) & 0x00ff;
        case 13: *size = 1; val = VM_MEM_READ8 (vm, vm->epc); vm->epc += 1; return val;
        case 14: *size = 2; val = VM_MEM_READ16(vm, vm->epc); vm->epc += 2; return val;
        case 15: *size = 4; val = VM_MEM_READ32(vm, vm->epc); vm->epc += 4; return val;
        }
    }
    return 0;
}

int ffvm_run(void *ctx)
{
    uint8_t opcode, opndtype, srcsize, dstsize;
    uint32_t opnd1, opnd2, *opndptr;
    FFVM *vm = (FFVM*)ctx;
    if (!ctx) return -1;

    opcode = VM_MEM_READ8(vm, vm->epc); vm->epc++;
    if (opcode >= 0 && opcode <= 0x0B) {
        switch (opcode) {
        case 0x00: break; // nop
        case 0x01: return -1; // hlt
        case 0x02: break; // int
        case 0x03: break; // nop
        case 0x04: vm->eps &= ~PS_C; break; // clc
        case 0x05: vm->eps &= ~PS_Z; break; // clz
        case 0x06: vm->eps &= ~PS_N; break; // cln
        case 0x07: break; // nop
        case 0x08: vm->eps |=  PS_C; break; // slc
        case 0x09: vm->eps |=  PS_Z; break; // slz
        case 0x0A: vm->eps |=  PS_N; break; // sln
        case 0x0B: break; // nop
        case 0x0C: break; // nop
        case 0x0D: break; // nop
        case 0x0E: break; // nop
        case 0x0F: break; // nop
        }
        return 0;
    }

    opndtype = VM_MEM_READ8(vm, vm->epc); vm->epc++;
    if (opcode & (1 << 0)) {
        opnd1   = fetch_opnd_val(vm, opndtype & 0xF, &dstsize);
        opndptr = opnd1 < vm->mem_size ? (uint32_t*)&vm->mem_buf[opnd1] : NULL;
        opnd1   = opndptr ? *opndptr : 0;
        dstsize = 4;
    } else {
        opndptr = fetch_opnd_ptr(vm, opndtype & 0xF, &dstsize);
        opnd1   = opndptr ? *opndptr : fetch_opnd_val(vm, opndtype & 0xF, &dstsize);
    }

    opnd2 = fetch_opnd_val(vm, (opndtype >> 4) & 0xF, &srcsize);
    if (opcode & (1 << 1)) {
        opnd2   = VM_MEM_READ32(vm, opnd2);
        srcsize = dstsize;
    }
    if (opcode & (1 << 0)) dstsize = srcsize;

    switch (opcode >> 2) {
    case 0x03: opnd1  = opnd2; break; // mov
    case 0x04: opnd1 += opnd2; break; // add
    case 0x05: opnd1 += opnd2 + (vm->eps & (PS_C)); break; // adc
    case 0x06: opnd1 -= opnd2; break; // sub
    case 0x07: opnd1 -= opnd2 + (vm->eps & (PS_C)); break; // sbb
    case 0x08: opnd1 *= opnd2; break; // umul
    case 0x09: *(int32_t*)&opnd1 *= (int32_t)opnd2; break; // imul
    case 0x0A: opnd1 /= opnd2; break; // udiv
    case 0x0B: *(int32_t*)&opnd1 /= (int32_t)opnd2; break; // idiv
    case 0x0C: *(float*)&opnd1 += (float)opnd2; break; // fadd
    case 0x0D: *(float*)&opnd1 -= (float)opnd2; break; // fsub
    case 0x0E: *(float*)&opnd1 *= (float)opnd2; break; // fmul
    case 0x0F: *(float*)&opnd1 /= (float)opnd2; break; // fdiv
    case 0x10: opnd1 &= opnd2; break; // and
    case 0x11: opnd1 |= opnd2; break; // or
    case 0x12: opnd1  =~opnd1; break; // not
    case 0x13: opnd1 ^= opnd2; break; // xor
    }

    if (opndptr) memcpy(opndptr, &opnd1, dstsize);
    return 0;
}

#if 1
static uint8_t g_test_code[] = {
    0x08, // slc
    0x09, // slz
    0x0A, // sln

    0x04, // clc
    0x05, // clz
    0x06, // cln

    (3 << 2) | (0 << 0), 0xF0, 0x12, 0x34, 0x56, 0x78, // mov eax, imm32
    (3 << 2) | (0 << 0), 0xF1, 0x11, 0x22, 0x33, 0x44, // mov ebx, imm32
    (3 << 2) | (0 << 0), 0xF2, 0x55, 0x66, 0x77, 0x88, // mov ecx, imm32
    (3 << 2) | (0 << 0), 0xF3, 0x99, 0xAA, 0xBB, 0xCC, // mov edx, imm32
    (3 << 2) | (0 << 0), 0xF4, 0xDD, 0xEE, 0xFF, 0x00, // mov ebp, imm32
    (3 << 2) | (0 << 0), 0xF5, 0xEF, 0xCD, 0xAB, 0x89, // mov esp, imm32
    (3 << 2) | (0 << 0), 0xF6, 0x76, 0x54, 0x32, 0x11, // mov eps, imm32

    (3 << 2) | (1 << 0), 0x0E, 0x00, 0x80, // mov [0x8000], eax
    (3 << 2) | (1 << 0), 0x1E, 0x04, 0x80, // mov [0x8004], ebx
    (3 << 2) | (1 << 0), 0x2E, 0x08, 0x80, // mov [0x8008], ecx
    (3 << 2) | (1 << 0), 0x3E, 0x0C, 0x80, // mov [0x800C], edx
    (3 << 2) | (1 << 0), 0x4E, 0x10, 0x80, // mov [0x8010], ebp
    (3 << 2) | (1 << 0), 0x5E, 0x14, 0x80, // mov [0x8014], esp
    (3 << 2) | (1 << 0), 0x6E, 0x18, 0x80, // mov [0x8018], eps

    0x00, // nop
    0x01, // hlt
};

int main(void)
{
    void *vm = ffvm_init(64 * 1024);
    memcpy(((FFVM*)vm)->mem_buf, g_test_code, sizeof(g_test_code));
    while (ffvm_run(vm) == 0) {
        ffmv_dump(vm);
    }
    ffvm_exit(vm);
}
#endif













