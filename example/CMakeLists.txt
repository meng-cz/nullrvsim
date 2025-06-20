
enable_language(C ASM)

if($ENV{CROSS_COMPILE})
    set(CMAKE_CXX_COMPILER $ENV{CROSS_COMPILE}g++)
    set(CMAKE_C_COMPILER $ENV{CROSS_COMPILE}gcc)
    set(CMAKE_ASM_COMPILER $ENV{CROSS_COMPILE}gcc)
else()
    set(CMAKE_CXX_COMPILER riscv64-linux-gnu-g++)
    set(CMAKE_C_COMPILER riscv64-linux-gnu-gcc)
    set(CMAKE_ASM_COMPILER riscv64-linux-gnu-gcc)
endif()

set(CMAKE_C_FLAGS "-Wall -g -O2 -static -static-libgcc")
set(CMAKE_CXX_FLAGS ${CMAKE_C_FLAGS} )
set(CMAKE_ASM_FLAGS ${CMAKE_C_FLAGS} )

add_executable(helloworld.riscv helloworld.c)
add_executable(mutex_test.riscv mutex_test.c)
add_executable(pthread_test.riscv pthread_test.c)
add_executable(mp_test.riscv mp_test.c)
add_executable(exec_test.riscv exec_test.c)
add_executable(select_test.riscv select_test.c)
add_executable(pth_perf.riscv pth_perf.c)
