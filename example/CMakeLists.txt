
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

execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resource ${CMAKE_CURRENT_BINARY_DIR})

add_executable(helloworld.riscv helloworld.c)
add_executable(vecadd1core.riscv vecadd1core.c)
add_executable(vecadd1core_mmap.riscv vecadd1core_mmap.c)
add_executable(vecaddpt.riscv vecaddpt.c)
add_executable(mutex_test.riscv mutex_test.c)
