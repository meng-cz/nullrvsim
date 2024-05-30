
enable_language(C ASM)

if($ENV{CROSS_COMPILE})
    set(CMAKE_C_COMPILER $ENV{CROSS_COMPILE}gcc)
    set(CMAKE_ASM_COMPILER $ENV{CROSS_COMPILE}gcc)
else()
    set(CMAKE_C_COMPILER riscv64-linux-gnu-gcc)
    set(CMAKE_ASM_COMPILER riscv64-linux-gnu-gcc)
endif()

set(CMAKE_C_FLAGS "-s -march=rv64gc -nostdlib -nostartfiles -fPIC -flto" )
set(CMAKE_ASM_FLAGS ${CMAKE_C_FLAGS} )

add_library(ecallproxy SHARED ecall.c asm_ecall.s host_syscall.s memops.s)

