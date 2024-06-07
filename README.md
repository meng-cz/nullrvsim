# nullrvsim

(Working in Progress)

A Cycle-Driven RISC-V (RV64GC) User-Mode Simulator for Learning Computer Architecture

在学习计算机体系结构的时候搓的一个模拟器：

- 基于RV64GC指令集
- 模拟每一个硬件组件在每个周期的行为
- 只模拟用户态指令，ecall由模拟器代理到主机执行
- 多线程加速模拟 *（还没优化，现在最好还是单线程跑）*

## 目前的内容

CPU核心：

- 简单的五级流水线核心
- 仿照香山三期CPU流水线实现的乱序核心 *（还在测试）*

Cache与总线：

- 简单的广播总线
- 基于目录的MOESI一致性协议
- 模拟实现AMBA5里的CHI总线协议 *（正在做，等我学完）*
- 模拟实现TileLink总线协议 *（后续会做）*

软件接口：

- 直接加载RV64 Linux elf文件，支持动态链接elf
- 基于pthread的多线程，实现了简单的线程调度
- 支持部分常见系统调用 *（遇到一个实现一个）*
- *Fork还没做，不支持多进程*


## 构建

### 依赖：

- 我用的Linux内核版本：**5.15.\*** *（不同的内核版本可能导致主机系统调用接口不同，详细的版本支持信息还需测试）*
- 编译工具与内核头文件: **gcc, g++, cmake, linux-headers**
- RV交叉编译工具：**riscv64-linux-gnu-gcc**


### 编译：

```bash
mkdir build
cd build
cmake ..
make -j16
```

### 编译后的内容：

- **build/nullrvsim**: 模拟器的可执行文件
- **build/conf/default.ini**: 默认配置文件，可通过命令行参数-c重新指定
- **build/rvsrc/ecallproxy/libecallproxy.so**: 用于辅助处理系统调用的rv库文件，如有移动需在conf配置文件中指定
- **build/example/\***: 用于测试的RV64GC可执行文件与相关数据文件


## 运行

### 命令行参数

```bash
./nullrvsim operation [-c configs] [-w workload argvs]
```

**operation:** 运行项目，目前只有mp_moesi_l1l2这一项，启动src/launch/l1l2.cpp中的内容。其他测试项详见main.cpp。

**-c (可选):** 配置文件ini的路径，默认为conf/default.ini。

**-w :** Workload的ELF文件路径与argv。-w后的所有内容都会作为argv传给模拟程序，其他参数必须在-w之前。

暂时不支持修改模拟进程的环境变量与Aux-Vec。

### 配置模拟CPU核心数

修改conf/default.ini：

```ini
[multicore]
cpu_number = 4
mem_size_mb = 256
```

### 运行单线程程序测试：

运行example中静态链接的rv64gc elf文件：
```bash
# pwd : .../build
./nullrvsim mp_moesi_l1l2 -w example/helloworld.riscv
```

运行动态链接的elf文件：

修改conf/default.ini，将ldpath设置为ld-linux-riscv64-lp64d.so.1所在目录：
```ini
[workload]
ld_path = /usr/riscv64-linux-gnu/lib
```
使用默认选项gcc交叉编译：
```bash
# pwd : .../build
riscv64-linux-gnu-gcc ../example/helloworld.c -o helloworld.dyn.riscv
./nullrvsim mp_moesi_l1l2 -w helloworld.dyn.riscv
```

### 运行pthread多线程程序测试：

```bash
# pwd : .../build
./nullrvsim mp_moesi_l1l2 -w example/vecaddpt.riscv 4 2048 example/i2048-a.txt example/i2048-b.txt example/i2048-asumb.txt
```

## 感谢

使用/包含的项目：
- Easylogging++: [abumq/easyloggingpp](https://github.com/abumq/easyloggingpp)
- ELFIO: [serge1/ELFIO](https://github.com/serge1/ELFIO)

参考的项目：
- 香山开源CPU: [OpenXiangShan/XiangShan](https://github.com/OpenXiangShan/XiangShan), [XiangShan官方文档](https://xiangshan-doc.readthedocs.io/zh-cn/latest/)
- BOOM: [riscv-boom/riscv-boom](https://github.com/riscv-boom/riscv-boom), [RISCV-BOOM's Documentation](https://docs.boom-core.org/en/latest/)




