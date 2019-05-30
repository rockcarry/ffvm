typedef struct {
    #define PS_C (1 << 0)
    #define PS_Z (1 << 1)
    #define PS_N (1 << 2)
    uint32_t eax;
    uint32_t ebx;
    uint32_t ebp;
    uint32_t esp;
    uint32_t epc;
    uint32_t eps;
    uint8_t *mem_buf ;
    uin32_t  mem_size;
} FFVM;

#define VM_MEM_READ8(vm, addr)        ((addr) < (vm)->mem_size ? (vm)->mem_buf[addr] : 0xff)
#define VM_MEM_WRITE8(vm, addr, val)  do { if ((addr) < (vm)->mem_size) (vm)->mem_buf[addr] = val; } while (0)

static uint16_t VM_MEM_READ16(FFVM *vm, uint32_t addr)
{
    return addr + 1 < vm->mem_size ? *(uint16_t*)&vm->mem_buf[addr] : 0xffff;
}

static void VM_MEM_WRITE16(FFVM *vm, uint32_t addr, uint16_t val)
{
    if (addr + 1 < vm->mem_size) *(uint16_t*)&vm->mem_buf[addr] = val;
}

static uint32_t VM_MEM_READ32(FFVM *vm, uint32_t addr)
{
    return addr + 3 < vm->mem_size ? *(uint32_t*)&vm->mem_buf[addr] : 0xffffffff;
}

static void VM_MEM_WRITE32(FFVM *vm, uint32_t addr, uint32_t val)
{
    if (addr + 3 < vm->mem_size) *(uint32_t*)&vm->mem_buf[addr] = val;
}


void* ffvm_init(int memsize)
{
    FFVM *vm = malloc(sizeof(FFVM));
    if (!vm) return NULL;
    vm->mem_buf = malloc(memsize);
    vm->mem_size= vm->mem_buf ? 0 : memsize;
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
    if (vm) vm->eax = vm->ebx = vm->ebp = vm->esp = vm->epc = vm->eps = 0;
}

int ffvm_run(void *ctx)
{
    uint8_t   opcode, optr;
    uint8_t  *p8dst , *p8src;
    uint16_t *p16dst, *p16src;
    uint32_t *p32dst, *p32src;
    FFVM *vm = (FFVM*)ctx;
    if (!ctx) return -1;

    opcode = VM_MEM_READB(vm, vm->epc); vm->epc++;
    switch (opcode) {
    case 0x1C: vm->esp-= 4; vm->epc = VM_MEM_READ32(vm, vm->esp); continue; // ret
    case 0x21: vm->eps&=~PS_C;    continue; // clc
    case 0x26: vm->eax = vm->eps; continue; // mov eax, eps
    case 0x2B: vm->eps = vm->eax; continue; // mov eps, eax
    }

    switch (opcode >> 2) {
    case 0x01:
    case 0x02:
        if (opcode & (1 << 2)) {
            p8dst  = (uint8_t*)&vm->eax + !!(opcode & (1 << 1));
        } else {
            p8dst  = (uint8_t*)&vm->ebx + !!(opcode & (1 << 1));
        }
        if (opcode & (1 << 0)) {
            *p8dst = VM_MEM_READ8(vm, vm->epc); vm->epc++;
        } else {
            *p8dst = VM_MEM_READ8(vm, VM_MEM_READ32(vm, vm->epc)); vm->epc += 4;
        }
        break;
    case 0x03:
        if (opcode & (1 << 1)) {
            p16dst  = (uint16_t*)&vm->eax;
        } else {
            p16dst  = (uint16_t*)&vm->ebx;
        }
        if (opcode & (1 << 0)) {
            *p16dst = VM_MEM_READ16(vm, vm->epc); vm->epc += 2;
        } else {
            *p16dst = VM_MEM_READ16(vm, VM_MEM_READ32(vm, vm->epc)); vm->epc += 4;
        }
        break;
    case 0x04:
        if (opcode & (1 << 1)) {
            p32dst  = (uint32_t*)&vm->eax;
        } else {
            p32dst  = (uint32_t*)&vm->ebx;
        }
        if (opcode & (1 << 0)) {
            *p32dst = VM_MEM_READ32(vm, vm->epc); vm->epc += 4;
        } else {
            *p32dst = VM_MEM_READ32(vm, VM_MEM_READ32(vm, vm->epc)); vm->epc += 4;
        }
        break;
    case 0x05:
        if (opcode & (1 << 1)) {
            p8src  = (uint8_t*)&vm->eax + (opcode & (1 << 0));
        } else {
            p8src  = (uint8_t*)&vm->ebx + (opcode & (1 << 0));
        }
        VM_MEM_WRITE8(vm, VM_MEM_READ32(vm, vm->epc), *p8src); vm->epc += 4;
        break;
    case 0x06:
        if (opcode & (1 << 1)) {
            p16src = opcode & (1 << 0) ? (uint16_t*)vm->eax : (uint16_t*)vm->ebx;
            VM_MEM_WRITE16(vm, VM_MEM_READ32(vm, vm->epc), *p16src); vm->epc += 4;
        } else {
            p32src = opcode & (1 << 0) ? (uint32_t*)vm->eax : (uint32_t*)vm->ebx;
            VM_MEM_WRITE32(vm, VM_MEM_READ32(vm, vm->epc), *p32src); vm->epc += 4;
        }
        break;
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0A:
        if (opcode & (1 << 1)) {
            p8src = (uint8_t)&vm->eax + (opcode & (1 << 0));
        } else {
            p8src = (uint8_t)&vm->ebx + (opcode & (1 << 0));
        }
        if (opcode & (1 << 2)) {
            *((uint8_t*)&vm->eax + (1 << 0)) = *p8src;
        } else {
            *((uint8_t*)&vm->ebx + (1 << 0)) = *p8src;
        }
        break;
    case 0x0B:
        if (opcode & (1 << 1)) {
            if (opcode & (1 << 0)) {
                vm->eps &= ~PS_N;
            } else {
                vm->eps &= ~PS_Z;
            }
        } else {
            if (opcode & (1 << 0)) {
                *(uint16_t*)&vm->ebx = *(uint16_t*)&vm->eax;
            } else {
                *(uint16_t*)&vm->eax = *(uint16_t*)&vm->ebx;
            }
        }
        break;
    case 0x0C:
    case 0x0D:
        if (opcode & (1 << 2)) {
            if (opcode & (3 << 0)) {
                vm->ebx = *((uint32_t*)&vm->ebx + (opcode & (3 << 0)))
            } else {
                vm->ebx = vm->eax;
            }
        } else {
            vm->eax = *((uint32_t*)&vm->ebx + (opcode & (3 << 0)));
        }
        break;
    case 0x0E:
        *((uint32_t*)vm->ebp + !!(opcode & (1 << 1))) = *((uint32_t*)vm->eax + (opcode & (1 << 0)));
        break;
    }
}
















