#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <conio.h>

typedef unsigned long long uint64_t;
typedef long long           int64_t;
typedef unsigned           uint32_t;
typedef int                 int32_t;
typedef unsigned short     uint16_t;
typedef short               int16_t;
typedef unsigned char      uint8_t;
typedef char                int8_t;
#define get_tick_count GetTickCount
#define usleep(t)      Sleep((t)/1000)

typedef struct {
    uint32_t x[32];
    uint32_t pc;
    uint64_t f[32];
    uint32_t fcsr;
    #define MAX_MEM_SIZE (8 * 1024 * 1024)
    uint8_t  mem[MAX_MEM_SIZE];
    uint32_t heap;
    #define TS_EXIT (1 << 0)
    uint32_t status;
} RISCV;

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
    if ((addr & 0x3) == 0) {
        return *(uint32_t*)(riscv->mem + (addr & (MAX_MEM_SIZE - 1)));
    } else {
        return (riscv->mem[(addr + 0) & (MAX_MEM_SIZE - 1)] << 0)
             | (riscv->mem[(addr + 1) & (MAX_MEM_SIZE - 1)] << 8)
             | (riscv->mem[(addr + 2) & (MAX_MEM_SIZE - 1)] <<16)
             | (riscv->mem[(addr + 3) & (MAX_MEM_SIZE - 1)] <<24);
    }
}

static void riscv_memw32(RISCV *riscv, uint32_t addr, uint32_t data)
{
    if (addr == 0x8000) {
        putchar((int)data);
        return;
    }
    if ((addr & 0x3) == 0) {
        *(uint32_t*)(riscv->mem + (addr & (MAX_MEM_SIZE - 1))) = data;
    } else {
        riscv->mem[(addr + 0) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >> 0);
        riscv->mem[(addr + 1) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >> 8);
        riscv->mem[(addr + 2) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >>16);
        riscv->mem[(addr + 3) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >>24);
    }
}

static int32_t signed_extend(uint32_t a, int size)
{
    return (a & (1 << (size - 1))) ? (a | ~((1 << size) - 1)) : a;
}

static uint32_t handle_ecall(RISCV *riscv)
{
    switch (riscv->x[17]) {
    case 93: riscv->status |= TS_EXIT; return 0; //sys_exit
    default: return 0;
    }
}

static void riscv_execute_rv16(RISCV *riscv, uint16_t instruction)
{
}

static void riscv_execute_rv32(RISCV *riscv, uint32_t instruction)
{
    const uint32_t inst_opcode = (instruction >> 0) & 0x7f;
    const uint32_t inst_rd     = (instruction >> 7) & 0x1f;
    const uint32_t inst_funct3 = (instruction >>12) & 0x07;
    const uint32_t inst_rs1    = (instruction >>15) & 0x1f;
    const uint32_t inst_rs2    = (instruction >>20) & 0x1f;
    const uint32_t inst_funct7 = (instruction >>25) & 0x7f;
    const uint32_t inst_imm12i = (instruction >>20) & 0xfff;
    const uint32_t inst_imm12s =((instruction >>20) & (0x7f << 5)) | ((instruction >> 7) & 0x1f);
    const uint32_t inst_imm13b =((instruction >>19) & (0x1  <<12)) | ((instruction << 4) & (0x1 << 11))
                               |((instruction >>20) & (0x3f << 5)) | ((instruction >> 7) & (0xf <<  1));
    const uint32_t inst_imm20u = instruction & (0xfffff << 12);
    const uint32_t inst_imm21j =((instruction >> 11) & (1 << 20)) | (instruction & (0xff << 12))
                               |((instruction >> 9) & (1 << 11)) | ((instruction >> 20) & (0x3ff << 1));
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
        case 0x0: riscv->x[inst_rd] = (int32_t)riscv_memr8 (riscv, maddr); break; // lb
        case 0x1: riscv->x[inst_rd] = (int32_t)riscv_memr16(riscv, maddr); break; // lh
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
        if (instruction & (1 << 20)) { // ebreak;
        } else { // ecall
            riscv->x[10] = handle_ecall(riscv);
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

#define RISCV_CPU_FREQ  (1*1000*1000)
#define RISCV_FRAMERATE  50

int main(int argc, char *argv[])
{
    char romfile[MAX_PATH] = "rom.bin";
    uint32_t next_tick = 0;
    int32_t  sleep_tick= 0;
    RISCV    *riscv = NULL;
    FILE     *fp    = NULL;
    int      i;

    riscv = calloc(1, sizeof(RISCV));
    if (!riscv) return 0;

    if (argc >= 2) {
        strncpy(romfile, argv[1], sizeof(romfile));
    }

    riscv->pc = 0x1008c; // startup addr
    fp = fopen(romfile, "rb");
    if (fp) {
        fread(riscv->mem + 0x10074, 1, sizeof(riscv->mem) - 0x10074, fp);
        fclose(fp);
    }

    while (!(riscv->status & (TS_EXIT))) {
        if (!next_tick) next_tick = get_tick_count();
        next_tick += 1000 / RISCV_FRAMERATE;
        for (i=0; i<RISCV_CPU_FREQ/RISCV_FRAMERATE; i++) {
            riscv_run(riscv);
        }
        sleep_tick = next_tick - get_tick_count();
        if (sleep_tick > 0) usleep(sleep_tick * 1000);
//      printf("sleep_tick: %d\n", sleep_tick);
    }

    free(riscv);
    getch();
    return 0;
}
