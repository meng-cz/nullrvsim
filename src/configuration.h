#ifndef RVSIM_CONFIGURATION_H
#define RVSIM_CONFIGURATION_H

#include "common.h"

namespace conf {

void load_ini_file(string filepath);

int64_t get_int(string sec, string name, int64_t def = 0);

float get_float(string sec, string name, float def = 0.f);

string get_str(string sec, string name, string def = "");

}

namespace test {

bool test_ini_file();

}

#endif
