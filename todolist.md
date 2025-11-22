# NullSim 开发记录与任务书

## v1.1.0 RV64A23指令集功能支持

### RV64 通用解码框架 **完成时间20251116**

- `src/cpu/riscv64.h`: 寄存器信息+执行单元选择+执行单元参数 的指令解码框架

### RV64 I/A 计算操作 **完成时间20251120**

- `src/cpu/intop.*`: 整数执行单元算术操作定义与实现，ALU，MUL，DIV
- `src/cpu/amoop.*`: AMO执行单元算术操作定义与实现，AMO

### RV64 F/D/Q/Zfh 软浮点计算操作

- `src/float/floatop.h`: 软浮点基础类型定义，舍入模式定义，浮点异常定义，浮点算术操作定义
- `src/float/float16.cpp`: 16位浮点算术操作实现+单元测试
- `src/float/float32.cpp`: 32位浮点算术操作实现+单元测试
- `src/float/float64.cpp`: 64位浮点算术操作实现+单元测试
- `src/float/float128.cpp`: 128位浮点算术操作实现+单元测试

- `src/cpu/fpop.*`: 浮点执行单元算术操作定义与实现，FALU，FCVT，FCMP，FMUL，FDIV，FSQRT，FMA，I2F，F2I

### RV64 B/K 位运算和加解密计算操作

- `src/cpu/bitop.*`: 位运算执行单元算术操作定义与实现，BITS
- `src/cpu/cryptoop.*`: 加解密执行单元算术操作定义与实现，CRYPTO

### RV64 V 向量计算操作

- `src/cpu/veciop.*`: 整数向量执行单元算术操作定义与实现，VIALU，VIMUL，VIDIV，VIMAC，VIPU，VPPU，VMOVE，I2V
- `src/cpu/vecfop.*`: 浮点向量执行单元算术操作定义与实现，VFALU，VFMA，VFCVT，VFMUL，VFDIV，VFSQRT，VFMAC，F2V

