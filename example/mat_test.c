#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAT_M 4
#define MAT_K 16
#define MAT_N 4

/* 矩阵指令编码（32位 custom‑1 opcode = 0x2b） */
#define INST_MLAE8(trd, rs1, rs2) \
  (0x2b | (0<<28) | (1<<26) | (0<<25) | ((rs2)<<20) | ((rs1)<<15) | (0<<12) | (0<<10) | ((trd)<<7))

#define INST_MLBE8(trd, rs1, rs2) \
  (0x2b | (1<<28) | (1<<26) | (0<<25) | ((rs2)<<20) | ((rs1)<<15) | (0<<12) | (0<<10) | ((trd)<<7))

#define INST_MSCE32(trs, rs1, rs2) \
  (0x2b | (2<<28) | (1<<26) | (1<<25) | ((rs2)<<20) | ((rs1)<<15) | (0<<12) | (2<<10) | ((trs)<<7))

#define INST_MMACC_W_B(md, ms2, ms1) \
  (0x2b | (1<<28) | (2<<26) | (0x3<<23) | ((ms2)<<20) | (0<<18) | ((ms1)<<15) | (0<<12) | (2<<10) | ((md)<<7))

#define INST_MZERO(md) \
  (0x2b | (0<<28) | (3<<26) | (0<<23) | (0<<20) | (0<<15) | (0<<12) | (0<<10) | ((md)<<7))

#define INST_MRELEASE() \
  (0x2b | (0<<28) | (0<<26) | (0<<25) | (0<<20) | (0<<15) | (0<<12) | (0<<10) | (0<<7))

int main() {
    int8_t A[MAT_M][MAT_K];
    int8_t B[MAT_N][MAT_K];          // 以转置形式存储：B[j][k] 对应矩阵 B 的 k 行 j 列
    int32_t C_expected[MAT_M][MAT_N];
    int32_t C_hw[MAT_M][MAT_N];

    /* 初始化输入数据 */
    for (int i = 0; i < MAT_M; i++)
        for (int k = 0; k < MAT_K; k++)
            A[i][k] = (int8_t)(i * MAT_K + k);

    for (int j = 0; j < MAT_N; j++)
        for (int k = 0; k < MAT_K; k++)
            B[j][k] = (int8_t)(j * MAT_K + k + 1);

    /* 软件计算期望结果 */
    for (int i = 0; i < MAT_M; i++)
        for (int j = 0; j < MAT_N; j++) {
            int32_t sum = 0;
            for (int k = 0; k < MAT_K; k++)
                sum += (int32_t)A[i][k] * (int32_t)B[j][k];
            C_expected[i][j] = sum;
        }

    memset(C_hw, 0, sizeof(C_hw));

    uint64_t addrA   = (uint64_t)A;
    uint64_t addrB   = (uint64_t)B;
    uint64_t addrC   = (uint64_t)C_hw;
    int strideA = MAT_K * sizeof(int8_t);
    int strideB = MAT_K * sizeof(int8_t);
    int strideC = MAT_N * sizeof(int32_t);

    printf("Testing matrix instructions...\n");

    /* 内联汇编：设置地址 -> 执行矩阵指令 */
    asm volatile (
        "mv a0, %[baseA]\n\t"
        "mv a1, %[strideA]\n\t"
        "mv a2, %[baseB]\n\t"
        "mv a3, %[strideB]\n\t"
        "mv a4, %[baseC]\n\t"
        "mv a5, %[strideC]\n\t"
        ".word %[iz]\n\t"       /* mzero   acc0            */
        ".word %[ila]\n\t"      /* mlae8   tr0, (a0), a1   */
        ".word %[ilb]\n\t"      /* mlbe8   tr1, (a2), a3   */
        ".word %[ima]\n\t"      /* mmacc.w.b acc0, tr1, tr0 */
        ".word %[isc]\n\t"      /* msce32  acc0, (a4), a5  */
        ".word %[irel]\n\t"     /* mrelease                */
        :
        : [baseA]   "r" (addrA),
          [strideA] "r" (strideA),
          [baseB]   "r" (addrB),
          [strideB] "r" (strideB),
          [baseC]   "r" (addrC),
          [strideC] "r" (strideC),
          [iz]      "i" (INST_MZERO(4)),               // md = 4 (acc0)
          [ila]     "i" (INST_MLAE8(0, 10, 11)),       // tr0, a0, a1
          [ilb]     "i" (INST_MLBE8(1, 12, 13)),       // tr1, a2, a3
          [ima]     "i" (INST_MMACC_W_B(4, 1, 0)),     // acc0 += tr1 * tr0
          [isc]     "i" (INST_MSCE32(4, 14, 15)),      // store acc0 -> (a4), stride a5
          [irel]    "i" (INST_MRELEASE())
        : "a0", "a1", "a2", "a3", "a4", "a5", "memory"
    );

    /* 比较硬件结果与期望值 */
    int pass = 1;
    for (int i = 0; i < MAT_M; i++)
        for (int j = 0; j < MAT_N; j++) {
            if (C_hw[i][j] != C_expected[i][j]) {
                printf("Mismatch at [%d][%d]: expected %d, got %d\n",
                       i, j, C_expected[i][j], C_hw[i][j]);
                pass = 0;
            }
        }

    if (pass)
        printf("Matrix test PASSED!\n");
    else
        printf("Matrix test FAILED!\n");

    return pass ? 0 : 1;
}
