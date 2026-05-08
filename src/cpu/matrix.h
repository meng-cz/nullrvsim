// MIT License

#ifndef RVSIM_CPU_MATRIX_H
#define RVSIM_CPU_MATRIX_H

#include "common.h"
#include "simerror.h"

namespace simcpu {

namespace matrix {

static constexpr int MAT_M = 4;
static constexpr int MAT_K = 16;
static constexpr int MAT_N = 4;
static constexpr int TILE_REG_CNT = 4;
static constexpr int ACC_REG_CNT = 4;
static constexpr int MAT_REG_CNT = TILE_REG_CNT + ACC_REG_CNT;
static constexpr uint8_t ACC_REG_BASE = 4;
static constexpr uint8_t MAT_TR0 = 0;
static constexpr uint8_t MAT_TR1 = 1;
static constexpr uint8_t MAT_TR2 = 2;
static constexpr uint8_t MAT_TR3 = 3;
static constexpr uint8_t MAT_ACC0 = 4;
static constexpr uint8_t MAT_ACC1 = 5;
static constexpr uint8_t MAT_ACC2 = 6;
static constexpr uint8_t MAT_ACC3 = 7;

using TileReg = int8_t[MAT_M][MAT_K];
using AccReg = int32_t[MAT_M][MAT_N];

inline bool is_tile_reg(uint8_t index) {
    return index < TILE_REG_CNT;
}

inline bool is_acc_reg(uint8_t index) {
    return index >= ACC_REG_BASE && index < MAT_REG_CNT;
}

inline uint8_t acc_index(uint8_t index) {
    return index - ACC_REG_BASE;
}

class MatrixState {
public:
    void clear();
    SimError zero(uint8_t reg);
    SimError release();
    SimError write_tile(uint8_t reg, const TileReg &value);
    SimError read_acc(uint8_t reg, AccReg &value) const;
    SimError macc_w_b(uint8_t md, uint8_t ms1, uint8_t ms2);

    uint32_t xmisa = 0x3;

private:
    int8_t tile_regs[TILE_REG_CNT][MAT_M][MAT_K];
    int32_t acc_regs[ACC_REG_CNT][MAT_M][MAT_N];
};

} // namespace matrix

} // namespace simcpu

#endif
