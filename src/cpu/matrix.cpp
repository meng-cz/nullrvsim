// MIT License

#include "cpu/matrix.h"

namespace simcpu {

namespace matrix {

void MatrixState::clear() {
    memset(tile_regs, 0, sizeof(tile_regs));
    memset(acc_regs, 0, sizeof(acc_regs));
}

SimError MatrixState::zero(uint8_t reg) {
    if(is_acc_reg(reg)) {
        memset(acc_regs[acc_index(reg)], 0, sizeof(acc_regs[0]));
        return SimError::success;
    }
    if(is_tile_reg(reg)) {
        memset(tile_regs[reg], 0, sizeof(tile_regs[0]));
        return SimError::success;
    }
    return SimError::illegalinst;
}

SimError MatrixState::release() {
    return SimError::success;
}

SimError MatrixState::write_tile(uint8_t reg, const TileReg &value) {
    if(!is_tile_reg(reg)) {
        return SimError::illegalinst;
    }
    memcpy(tile_regs[reg], value, sizeof(tile_regs[reg]));
    return SimError::success;
}

SimError MatrixState::read_acc(uint8_t reg, AccReg &value) const {
    if(!is_acc_reg(reg)) {
        return SimError::illegalinst;
    }
    memcpy(value, acc_regs[acc_index(reg)], sizeof(acc_regs[0]));
    return SimError::success;
}

SimError MatrixState::macc_w_b(uint8_t md, uint8_t ms1, uint8_t ms2) {
    if(!is_acc_reg(md) || !is_tile_reg(ms1) || !is_tile_reg(ms2)) {
        return SimError::illegalinst;
    }
    for(int i = 0; i < MAT_M; i++) {
        for(int j = 0; j < MAT_N; j++) {
            int32_t sum = 0;
            for(int k = 0; k < MAT_K; k++) {
                sum += (int32_t)tile_regs[ms1][i][k] * (int32_t)tile_regs[ms2][j][k];
            }
            acc_regs[acc_index(md)][i][j] += sum;
        }
    }
    return SimError::success;
}

} // namespace matrix

} // namespace simcpu
