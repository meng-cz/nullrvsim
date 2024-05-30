#ifndef RVSIM_LAUNCH_H
#define RVSIM_LAUNCH_H

#include "common.h"
#include "configuration.h"

namespace launch {

bool sp_moesi_l1l2(std::vector<string> &argv);

bool mp_moesi_l1l2(std::vector<string> &argv);

inline string generate_ld_library_path_str() {
    return string("LD_LIBRARY_PATH=") + conf::get_str("workload", "ld_path", "");
}

}

#endif
