
.section .text

.global _host_syscall_1
_host_syscall_1:
    mv  a7,a1
    ecall 
    ret 


.global _host_syscall_2
_host_syscall_2:
    mv  a7,a2
    ecall 
    ret 

.global _host_syscall_3
_host_syscall_3:
    mv  a7,a3
    ecall 
    ret 

.global _host_syscall_4
_host_syscall_4:
    mv  a7,a4
    ecall 
    ret 

.global _host_syscall_5
_host_syscall_5:
    mv  a7,a5
    ecall 
    ret 

.global _host_syscall_6
_host_syscall_6:
    mv  a7,a6
    ecall 
    ret 

