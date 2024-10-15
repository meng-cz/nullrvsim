#ifndef RVSIM_LAUNCH_H
#define RVSIM_LAUNCH_H

#include "common.h"
#include "configuration.h"

namespace launch {

bool sp_moesi_l1l2(std::vector<string> &argv);

bool mp_moesi_l1l2(std::vector<string> &argv);

bool mp_moesi_l3(std::vector<string> &argv);

}

#endif
