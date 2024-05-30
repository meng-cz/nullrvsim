#ifndef RVSIM_SIM_STATISTIC_H
#define RVSIM_SIM_STATISTIC_H

#include "common.h"

typedef struct {
    uint64_t    processed_tick_cnt      = 0;
    uint64_t    halt_tick_cnt           = 0;
    uint64_t    finished_inst_cnt       = 0;
    
} CPUStat;







#endif
