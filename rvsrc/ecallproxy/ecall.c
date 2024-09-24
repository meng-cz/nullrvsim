
#include "host_syscall.h"
#include "ioctl_cmd.h"
#include "fcntl_cmd.h"
#include "memops.h"

// void _rv64memcpy(void *dst, void *src, uint64_t sz) {
//     for(uint64_t i = 0; i < sz; i++) {
//         ((uint8_t*)dst)[i] = ((uint8_t*)src)[i];
//     }
// }

static void _simple_strcpy(uint8_t *dst, uint8_t *src) {
    while(*src) {
        *dst = *src;
        dst++;
        src++;
    }
    *dst = 0;
}
static uint64_t _simple_strlen(uint8_t *s) {
    uint64_t len = 0UL;
    while(*s) {
        len++;
        s++;
    }
}

static uint8_t * _host_malloc(uint64_t size) {
    uint8_t * ret = (uint8_t*)_host_syscall_1(size, 901);
    return ret;
}
static void _host_free(void *hbuf) {
    uint64_t ret = _host_syscall_1((uint64_t)hbuf, 902);
}
static void _ecall_ret(uint64_t retv) {
    __asm__ (
        "mv a0,%[arg0] \n ebreak"
        :
        :[arg0]"r"(retv)
    );
}

#define CEIL_DIV(x,y) (((x) + (y) - 1) / (y))
#define ALIGN(x,y) ((y)*CEIL_DIV((x),(y)))

/**
 * Syscall 17 getcwd - get current working directory
 * char *getcwd(char buf[.size], size_t size);
*/
void proxy_17_getcwd(uint8_t *buf, uint64_t size) {
    uint64_t ret = 0;
    uint8_t *hpathname = _host_malloc(size);
    ret = _host_syscall_2((uint64_t)hpathname, size, 1017);
    if(ret != 0) {
        _simple_strcpy(buf, hpathname);
        ret = (uint64_t)buf;
    }
    _host_free(hpathname);
    _ecall_ret(ret);
}

/**
 * Syscall 25 fcntl - manipulate file descriptor
 * int fcntl(int fd, int op, ... arg );
 */
void proxy_25_fcntl(int64_t fd, int64_t op, uint64_t arg) {
    int ret = 0;
    uint8_t *hostbuf = 0;
    int64_t bufsz = 0;

    switch (op)
    {
    case F_GETFD: case F_SETFD:
        goto fcntl_nobuf_out;
    }

    fcntl_nobuf_out:
    ret = _host_syscall_3(fd, op, arg, 1025);
    _ecall_ret(ret);

    fcntl_common_out:
    hostbuf = _host_malloc(bufsz);
    _rv64memcpy(hostbuf, (void*)arg, bufsz);
    ret = _host_syscall_3(fd, op, (uint64_t)hostbuf, 1025);
    _rv64memcpy((void*)arg, hostbuf, bufsz);
    _host_free(hostbuf);
    _ecall_ret(ret);

}

#define SIZEOF_STRUCT_TERMIOS (36UL)
#define SIZEOF_STRUCT_IREQ (40UL)
struct __ifconf {
    int           ifc_len; /* size of buffer */
    char         *ifc_buf; /* buffer address */
};

/**
 * Syscall 29 ioctl - control device
 * int ioctl(int fd, unsigned long request, ...);
*/
void proxy_29_ioctl(int64_t fd, int64_t cmd, uint64_t arg) {
    int64_t ret = 0;
    uint8_t *hostbuf = 0;
    int64_t bufsz = 0;

    switch (cmd)
    {
    case TCGETS: case TCSETS: case TCSETSW: case TCSETSF:
        bufsz = SIZEOF_STRUCT_TERMIOS;
        goto common_ioctl;
    case SIOCGIFFLAGS: case SIOCSIFFLAGS: case SIOCGIFADDR: case SIOCGIFDSTADDR: case SIOCGIFBRDADDR: case SIOCGIFNETMASK:
    case SIOCGIFMETRIC: case SIOCGIFMTU: case SIOCGIFHWADDR: case SIOCGIFINDEX:
        bufsz = SIZEOF_STRUCT_IREQ;
        goto common_ioctl;
    case SIOCGIFCONF:
        bufsz = ((struct __ifconf *)arg)->ifc_len;
        if(bufsz < 0) goto ioctl_error_out;
        hostbuf = _host_malloc(bufsz + 16UL + SIZEOF_STRUCT_IREQ);
        ((struct __ifconf *)hostbuf)->ifc_len = bufsz;
        ((struct __ifconf *)hostbuf)->ifc_buf = ((((struct __ifconf *)arg)->ifc_buf)?(char*)(ALIGN((uint64_t)hostbuf + 16UL, SIZEOF_STRUCT_IREQ)):0);
        ret = _host_syscall_4(fd, cmd, (uint64_t)hostbuf, cmd, 910);
        if(ret >= 0 && bufsz) {
            ((struct __ifconf *)arg)->ifc_len = ((struct __ifconf *)hostbuf)->ifc_len;
            _rv64memcpy(((struct __ifconf *)arg)->ifc_buf, ((struct __ifconf *)hostbuf)->ifc_buf, bufsz);
        }
        _host_free(hostbuf);
        _ecall_ret(ret);
    }

    ioctl_error_out:
    ret = _host_syscall_4(fd, 0, 0, cmd, 1029);
    _ecall_ret(ret);

    common_ioctl:
    hostbuf = _host_malloc(bufsz);
    _rv64memcpy(hostbuf, (void*)arg, bufsz);
    ret = _host_syscall_4(fd, cmd, (uint64_t)hostbuf, cmd, 1029);
    _rv64memcpy((void*)arg, hostbuf, bufsz);
    _host_free(hostbuf);
    _ecall_ret(ret);
}

/**
 * Syscall 48 faccessat - check user's permissions for a file
 * int faccessat(int dirfd, const char *pathname, int mode, int flags);
*/
void proxy_48_faccessat(uint64_t dfd, uint8_t *pathname, uint64_t mode, uint64_t flags) {
    uint64_t ret = 0;
    uint8_t *hpathname = _host_malloc(4096UL);
    _simple_strcpy(hpathname, pathname);
    ret = _host_syscall_4(dfd, (uint64_t)hpathname, mode, flags, 1048);
    _host_free(hpathname);
    _ecall_ret(ret);
}

/**
 * Syscall 56 openat - open a file relative to a directory file descriptor 
 * int openat(int dirfd, const char *pathname, int flags, mode_t mode);
*/
void proxy_56_openat(uint64_t dfd, uint8_t *pathname, int32_t flags, uint64_t mode) {
    uint64_t ret = 0;
    uint8_t *hpathname = _host_malloc(4096UL);
    _simple_strcpy(hpathname, pathname);
    ret = _host_syscall_4(dfd, (uint64_t)hpathname, flags, mode, 1056);
    _host_free(hpathname);
    _ecall_ret(ret);
}

/**
 * Syscall 59 pipe2 - create pipe
 * int pipe2(int pipefd[2], int flags);
 */
void proxy_59_pipe2(int32_t *arr_pipefd, int32_t flags) {
    uint64_t ret = 0;
    int32_t *hbuf = (int32_t *)_host_malloc(16);
    ret = _host_syscall_2((uint64_t)hbuf, flags, 1059);
    arr_pipefd[0] = hbuf[0];
    arr_pipefd[1] = hbuf[1];
    _host_free(hbuf);
    _ecall_ret(ret);
}

/**
 * Syscall 63 read - read from a file descriptor
 * ssize_t read(int fd, void buf[.count], size_t count);
*/
void proxy_63_read(uint64_t fd, uint8_t *buf, int32_t size) {
    uint64_t ret = 0;
    if(size > 0) {
        uint8_t *hbuf = _host_malloc(size);
        ret = _host_syscall_3(fd, (uint64_t)hbuf, size, 1063);
        _rv64memcpy(buf, hbuf, size);
        _host_free(hbuf);
    }
    _ecall_ret(ret);
}

/**
 * Syscall 64 write - write to a file descriptor
 * ssize_t write(int fd, const void buf[.count], size_t count);
*/
void proxy_64_write(uint64_t fd, uint8_t *buf, int32_t size) {
    uint64_t ret = 0;
    if(size > 0) {
        uint8_t *hbuf = _host_malloc(size);
        _rv64memcpy(hbuf, buf, size);
        ret = _host_syscall_3(fd, (uint64_t)hbuf, size, 1064);
        _host_free(hbuf);
    }
    _ecall_ret(ret);
}

struct iovec {
    uint8_t *iov_base;	/* Pointer to data.  */
    uint64_t iov_len;	/* Length of data.  */
};


/**
 * Syscall 66 writev
 * ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
*/
void proxy_66_writev(uint64_t fd, struct iovec *iov, int32_t iovcnt) {
    uint64_t ret = 0;
    uint64_t size = 0;
    for(int i = 0; i < iovcnt; i++) {
        size += iov[i].iov_len;
    }
    if(size > 0) {
        uint8_t *hbuf = _host_malloc(size);
        uint64_t offset = 0;
        for(int i = 0; i < iovcnt; i++) {
            _rv64memcpy(hbuf + offset, iov[i].iov_base, iov[i].iov_len);
            offset += iov[i].iov_len;
        }
        ret = _host_syscall_3(fd, (uint64_t)hbuf, size, 1064);
        _host_free(hbuf);
    }
    _ecall_ret(ret);
}

struct __timespec64 {
    uint64_t tv_sec;
    int32_t tv_nsec;
    int32_t pad0;
};

struct timespec {
    uint64_t tv_sec;
    uint64_t tv_usec;
};

struct pollfd {
    int32_t     fd;         /* file descriptor */
    int16_t     events;     /* requested events */
    int16_t     revents;    /* returned events */
};

/**
 * Syscall 73 ppoll - wait for some event on a file descriptor
 * int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *_Nullable tmo_p, const sigset_t *_Nullable sigmask);
*/
void proxy_73_ppoll(struct pollfd *fds, uint64_t nfds, struct timespec * tmo_p, uint8_t *sigmask /* Un-implemented */) {
    uint8_t * hbuf = _host_malloc(sizeof(struct timespec) + sizeof(struct pollfd) * nfds);
    struct timespec * host_tmo = (struct timespec *)hbuf;
    struct pollfd * host_fds = (struct pollfd *)(hbuf + sizeof(struct timespec));
    if(tmo_p) {
        host_tmo->tv_sec = tmo_p->tv_sec;
        host_tmo->tv_usec = tmo_p->tv_usec;
    }
    else {
        host_tmo = 0;
    }
    _rv64memcpy(host_fds, fds, sizeof(struct pollfd) * nfds);
    uint64_t ret = _host_syscall_4((uint64_t)host_tmo, nfds, (uint64_t)host_tmo, (uint64_t)sigmask, 1073);
    for(uint64_t i = 0; i < nfds; i++) {
        fds[i].revents = host_fds[i].revents;
    }
    _host_free(hbuf);
    _ecall_ret(ret);
}

/**
 * Syscall 78 readlinkat - read value of a symbolic link
 * ssize_t readlinkat(int dirfd, const char *restrict pathname, char *restrict buf, size_t bufsiz);
*/
void proxy_78_readlinkat(uint64_t dirfd, uint8_t *pathname, uint8_t *buf, uint64_t bufsiz) {
    uint64_t ret = 0;
    if(bufsiz > 0) {
        uint8_t *hpath = _host_malloc(bufsiz + 4096UL);
        uint8_t *hbuf = hpath + 4096;
        _simple_strcpy(hpath, pathname);
        ret = _host_syscall_4(dirfd, (uint64_t)hpath, (uint64_t)hbuf, bufsiz, 1078);
        _rv64memcpy(buf, hbuf, ret);
        _host_free(hpath);
    }
    _ecall_ret(ret);
}

struct rv_stat {
    uint64_t    st_dev;		/* Device.  */
    uint64_t    st_ino;		/* file serial number.	*/
    uint32_t    st_mode;		/* File mode.  */
    uint32_t    st_nlink;		/* Link count.  */
    uint32_t    st_uid;		/* User ID of the file's owner.  */
    uint32_t    st_gid;		/* Group ID of the file's group.  */
    uint64_t    st_rdev;		/* Device number, if device.  */
    uint64_t    __pad1;
    int64_t     st_size;		/* Size of file, in bytes.  */
    int64_t     st_blksize;	/* Optimal block size for I/O.  */
    int64_t     st_blocks;	/* Number 512-byte blocks allocated. */
    struct __timespec64 st_atim;
    struct __timespec64 st_mtim;
    struct __timespec64 st_ctim;
};

struct x64_stat {
    uint64_t    st_dev;      /* ID of device containing file */
    uint64_t    st_ino;      /* Inode number */
    uint64_t    st_nlink;    /* Number of hard links */
    uint32_t    st_mode;     /* File type and mode */
    uint32_t    st_uid;      /* User ID of owner */
    uint32_t    st_gid;      /* Group ID of owner */
    uint32_t    __pad0;
    uint64_t    st_rdev;     /* Device ID (if special file) */
    int64_t     st_size;     /* Total size, in bytes */
    int64_t     st_blksize;  /* Block size for filesystem I/O */
    int64_t     st_blocks;   /* Number of 512 B blocks allocated */
    int64_t     st_atime;			/* Time of last access.  */
    uint64_t    st_atimensec;	/* Nscecs of last access.  */
    int64_t     st_mtime;			/* Time of last modification.  */
    uint64_t    st_mtimensec;	/* Nsecs of last modification.  */
    int64_t     st_ctime;			/* Time of last status change.  */
    uint64_t    st_ctimensec;	/* Nsecs of last status change.  */
    int64_t     __glibc_reserved[3];
};

/**
 * Syscall 79 fstatat - get file status
 * int fstatat(int dirfd, const char *restrict pathname, struct stat *restrict statbuf, int flags);
*/
void proxy_79_newfstatat(uint64_t dirfd, uint8_t *pathname, struct rv_stat *buf, uint64_t flags) {
    uint64_t ret = 0;
    uint8_t *hpath = _host_malloc(4096UL + sizeof(struct x64_stat));
    _simple_strcpy(hpath, pathname);
    struct x64_stat *hbuf = (struct x64_stat *)(hpath + 4096UL);
    ret = _host_syscall_4(dirfd, (uint64_t)hpath, (uint64_t)hbuf, flags, 1079);
    buf->st_dev = hbuf->st_dev;
    buf->st_ino = hbuf->st_ino;
    buf->st_mode = hbuf->st_mode;
    buf->st_nlink = hbuf->st_nlink;
    buf->st_uid = hbuf->st_uid;
    buf->st_gid = hbuf->st_gid;
    buf->st_rdev = hbuf->st_rdev;
    buf->st_size = hbuf->st_size;
    buf->st_blksize = hbuf->st_blksize;
    buf->st_blocks = hbuf->st_blocks;
    buf->st_atim.tv_sec = hbuf->st_atime;
    buf->st_atim.tv_nsec = hbuf->st_atimensec;
    buf->st_mtim.tv_sec = hbuf->st_mtime;
    buf->st_mtim.tv_nsec = hbuf->st_mtimensec;
    buf->st_ctim.tv_sec = hbuf->st_ctime;
    buf->st_ctim.tv_nsec = hbuf->st_ctimensec;
    _host_free(hpath);
    _ecall_ret(ret);
}
/**
 * Syscall 80 fstat - get file status
 * int fstat(int fd, struct stat *statbuf);
*/
void proxy_80_fstat(uint64_t fd, struct rv_stat *buf) {
    uint64_t ret = 0;
    struct x64_stat *hbuf = (struct x64_stat *)(_host_malloc(sizeof(struct x64_stat)));
    ret = _host_syscall_2(fd, (uint64_t)hbuf, 1080);
    buf->st_dev = hbuf->st_dev;
    buf->st_ino = hbuf->st_ino;
    buf->st_mode = hbuf->st_mode;
    buf->st_nlink = hbuf->st_nlink;
    buf->st_uid = hbuf->st_uid;
    buf->st_gid = hbuf->st_gid;
    buf->st_rdev = hbuf->st_rdev;
    buf->st_size = hbuf->st_size;
    buf->st_blksize = hbuf->st_blksize;
    buf->st_blocks = hbuf->st_blocks;
    buf->st_atim.tv_sec = hbuf->st_atime;
    buf->st_atim.tv_nsec = hbuf->st_atimensec;
    buf->st_mtim.tv_sec = hbuf->st_mtime;
    buf->st_mtim.tv_nsec = hbuf->st_mtimensec;
    buf->st_ctim.tv_sec = hbuf->st_ctime;
    buf->st_ctim.tv_nsec = hbuf->st_ctimensec;
    _host_free(hbuf);
    _ecall_ret(ret);
}

/**
 * Syscall 93 exit - terminate the calling process
 * void exit(int status);
*/
void proxy_93_exit(int status) {
    uint64_t clear_tid_address = _host_syscall_1(0, 904);
    if(clear_tid_address) {
        *((uint32_t*)clear_tid_address) = 0;
    }
    uint64_t ret = _host_syscall_1(status, 1093);
    _ecall_ret(ret);
}


typedef struct {
    uint64_t    uaddr;
    uint32_t    fval;
    uint32_t    futex_op;
    uint32_t    val;
    uint32_t    val2;
    uint64_t    uaddr2;
    uint32_t    fval2;
    uint32_t    val3;
} HostFutexArgs;

const uint32_t futex_wait = 0;
const uint32_t futex_wait_bitset = 9;
const uint32_t futex_wake = 1;
const uint32_t futex_wake_bitset = 10;

/**
 * Syscall 98 futex - fast user-space locking
 * long SYS_futex (uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3);
*/
void proxy_98_futex(uint32_t *uaddr, int futex_op, uint32_t val, struct timespec *timeout, uint32_t *uaddr2, uint32_t val3) {
    HostFutexArgs *args = (HostFutexArgs *)_host_malloc(sizeof(HostFutexArgs));
    args->futex_op = futex_op;
    args->val = val;
    args->val2 = (uint64_t)timeout;
    args->val3 = val3;
    args->uaddr = args->uaddr2 = args->fval = args->fval2 = 0;

    int raw_op = (futex_op & 127);
    // int use_futex2 = (raw_op == 3) || (raw_op == 4) || (raw_op == 5);

    args->uaddr = (uint64_t)uaddr;
    // args->uaddr2 = (use_futex2?((uint64_t)uaddr2):0);

    if(raw_op == futex_wait || raw_op == futex_wait_bitset) args->fval = *uaddr;
    // if(use_futex2) {
    //     args->fval2 = *uaddr2;
    // }

    uint64_t ret = _host_syscall_1((uint64_t)(args), 1098);
    
    _host_free(args);
    _ecall_ret(ret);
}

/**
 * Syscall 113 clock_gettime - clock and time functions
 * int clock_gettime(clockid_t clockid, struct timespec *tp);
*/
void proxy_113_clockgettime(uint64_t clockid, struct timespec *tp) {
    uint64_t ret = 0;
    struct timespec *htp = (struct timespec *)_host_malloc(sizeof(struct timespec));
    ret = _host_syscall_2(clockid, (uint64_t)htp, 1113);
    tp->tv_sec = htp->tv_sec;
    tp->tv_usec = htp->tv_usec;
    _host_free(htp);
    _ecall_ret(ret);
}

#define SIZEOF_SIGSET_T (8UL)
#define SIZEOF_KERNEL_SIGACTION (16UL + SIZEOF_SIGSET_T)

/**
 * Syscall 134 sigaction - examine and change a signal action
 * int sigaction(int signum, const struct sigaction *_Nullable restrict act, struct sigaction *_Nullable restrict oldact);
*/
void proxy_134_sigaction(int64_t signum, uint8_t *act, uint8_t *oldact) {
    uint64_t ret = 0;
    uint8_t *hact = 0, *holdact = 0;
    if(act) {
        hact = _host_malloc(SIZEOF_KERNEL_SIGACTION);
        _rv64memcpy(hact, act, SIZEOF_KERNEL_SIGACTION);
    }
    if(oldact) {
        holdact = _host_malloc(SIZEOF_KERNEL_SIGACTION);
    }
    ret = _host_syscall_3(signum, (uint64_t)hact, (uint64_t)holdact, 1134);
    if(oldact) {
        _rv64memcpy(oldact, holdact, SIZEOF_KERNEL_SIGACTION);
        _host_free(holdact);
    }
    if(act) {
        _host_free(hact);
    }
    _ecall_ret(ret);
}

/**
 * Syscall 135 sigprocmask - examine and change blocked signals
 * int sigprocmask(int how, const sigset_t *_Nullable restrict set, sigset_t *_Nullable restrict oldset);
 * int syscall(SYS_rt_sigprocmask, int how, const kernel_sigset_t *_Nullable set, kernel_sigset_t *_Nullable oldset, size_t sigsetsize);

*/
void proxy_135_sigprocmask(int64_t how, uint8_t *set, uint8_t *oldset, uint64_t sigsetsize) {
    uint64_t ret = 0;
    uint8_t *hset = _host_malloc(sigsetsize);
    if(set) {
        _rv64memcpy(hset, set, sigsetsize);
        ret = _host_syscall_4(how, (uint64_t)hset, (oldset)?((uint64_t)hset):0, sigsetsize, 1135);
    }
    else {
        ret = _host_syscall_4(how, 0, (uint64_t)hset, sigsetsize, 1135);
    }
    if(oldset) {
        _rv64memcpy(oldset, hset, sigsetsize);
    }
    _host_free(hset);
    _ecall_ret(ret);
}


char uts_sysname[65] = "Linux";
char uts_nodename[65] = "fedora-riscv-2-8";
char uts_release[65] = "6.1.31";
char uts_version[65] = "#1 SMP Sun Oct 22 00:58:22 CST 2023";
char uts_machine[65] = "riscv64";
char uts_domainname[65] = "GNU/Linux";

struct utsname {
    char sysname[65];    /* Operating system name (e.g., "Linux") */
    char nodename[65];   /* Name within communications network
                            to which the node is attached, if any */
    char release[65];    /* Operating system release
                            (e.g., "2.6.28") */
    char version[65];    /* Operating system version */
    char machine[65];    /* Hardware type identifier */
    char domainname[65]; /* NIS or YP domain name */
};

/**
 * Syscall 160 uname - get name and information about current kernel
 * int uname(struct utsname *buf);
*/
void proxy_160_uname(struct utsname *buf_sim) {
    _simple_strcpy(buf_sim->sysname, uts_sysname);
    _simple_strcpy(buf_sim->nodename, uts_nodename);
    _simple_strcpy(buf_sim->release, uts_release);
    _simple_strcpy(buf_sim->version, uts_version);
    _simple_strcpy(buf_sim->machine, uts_machine);
    _simple_strcpy(buf_sim->domainname, uts_domainname);
    _ecall_ret(0);
}

#define SOCKADDR_SIZE (16)

/**
 * Syscall 199 socketpair - create a pair of connected sockets
 * int socketpair(int domain, int type, int protocol, int sv[2]);
 */
void proxy_199_socketpair(int32_t domain, int32_t type, int32_t protocal, int32_t *arr_sv) {
    uint64_t ret = 0;
    int32_t *hbuf = (int32_t *)_host_malloc(SOCKADDR_SIZE);
    ret = _host_syscall_4(domain, type, protocal, (uint64_t)hbuf, 1199);
    arr_sv[0] = hbuf[0];
    arr_sv[1] = hbuf[1];
    _host_free(hbuf);
    _ecall_ret(ret);
}

/**
 * Syscall 203 connect - initiate a connection on a socket
 * int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
 */
void proxy_203_connect(int32_t sockfd, uint64_t sockaddr, uint64_t addrlen) {
    uint64_t ret = 0;
    uint8_t *hbuf = _host_malloc(addrlen);
    _rv64memcpy(hbuf, (void*)sockaddr, addrlen);
    ret = _host_syscall_3(sockfd, (uint64_t)hbuf, addrlen, 1203);
    _host_free(hbuf);
    _ecall_ret(ret);
}

/**
 * Syscall 208 setsockopt - get and set options on sockets
 * int setsockopt(int sockfd, int level, int optname, const void optval[.optlen], socklen_t optlen);
 */
void proxy_208_setsockopt(int32_t sockfd, int32_t level, int32_t optname, uint8_t *optval, uint64_t optlen) {
    uint64_t ret = 0;
    uint8_t *hbuf = optval;
    if(optlen) {
        hbuf = _host_malloc(optlen);
        _rv64memcpy(hbuf, optval, optlen);
    }
    ret = _host_syscall_5(sockfd, level, optname, (uint64_t)hbuf, optlen, 1208);
    if(optlen) {
        _rv64memcpy(optval, hbuf, optlen);
        _host_free(hbuf);
    }
    _ecall_ret(ret);
}


struct rlimit {
    uint64_t rlim_cur;  /* Soft limit */
    uint64_t rlim_max;  /* Hard limit (ceiling for rlim_cur) */
};

/**
 * Syscall 261 prlimit - get/set resource limits
 * int prlimit(pid_t pid, int resource, const struct rlimit *_Nullable new_limit, struct rlimit *_Nullable old_limit);
*/
void proxy_261_prlimit(uint64_t pid, uint64_t resource, struct rlimit* new_limit, struct rlimit* old_limit) {
    struct rlimit* hbuf = (struct rlimit*)_host_malloc(sizeof(struct rlimit) * 2);
    struct rlimit* hnew = hbuf;
    struct rlimit* hold = hbuf + 1;
    if(new_limit) _rv64memcpy(hnew, new_limit, sizeof(struct rlimit));
    uint64_t ret = _host_syscall_4(pid, resource, (uint64_t)hnew, (uint64_t)hold, 1261);
    if(old_limit) _rv64memcpy(old_limit, hold, sizeof(struct rlimit));
    _ecall_ret(ret);
}

/**
 * Syscall 278 getrandom - obtain a series of random bytes
 * ssize_t getrandom(void buf[.buflen], size_t buflen, unsigned int flags);
*/
void proxy_278_getrandom(uint8_t *buf, uint64_t buflen, uint32_t flags) {
    uint64_t ret = 0;
    if(buflen > 0) {
        uint8_t *hbuf = _host_malloc(buflen);
        ret = _host_syscall_3((uint64_t)hbuf, buflen, flags, 1278);
        _rv64memcpy(buf, hbuf, ret);
        _host_free(hbuf);
    }
    _ecall_ret(ret);
}


/**
 * Sim-side page fault handler
 */
// void proxy_501_pagefault(uint8_t *dst, uint8_t *src, uint64_t sz) {
//     _rv64memcpy(dst, src, sz);
//     uint64_t ret = _host_syscall_1((uint64_t)src, 905); 
//     _ecall_ret(ret);
// }

