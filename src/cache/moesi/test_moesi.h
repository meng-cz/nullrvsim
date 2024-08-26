#ifndef RVSIM_TEST_TEST_MOESI_HPP
#define RVSIM_TEST_TEST_MOESI_HPP

namespace test {

bool test_moesi_l1_cache();

bool test_moesi_l1_dma();

bool test_moesi_cache_rand();

bool test_moesi_cache_seq();

bool test_moesi_l1l2_cache();

bool test_moesi_cache_l1l2l3_rand();

bool test_moesi_cache_l1l2l3_seq();

bool test_moesi_cache_l3nuca_rand();

}

#endif
