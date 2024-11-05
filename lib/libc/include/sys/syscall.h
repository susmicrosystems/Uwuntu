#ifndef SYS_SYSCALL_H
#define SYS_SYSCALL_H

#ifdef __cplusplus
extern "C" {
#endif

/* process */
#define SYS_exit        1
#define SYS_clone       2
#define SYS_kill        3
#define SYS_wait4       4
#define SYS_execveat    5
#define SYS_getpid      6
#define SYS_setpgid     7
#define SYS_getppid     8
#define SYS_getpgrp     9
#define SYS_setsid      10
#define SYS_getpgid     11
#define SYS_getsid      12
#define SYS_sigaction   13
#define SYS_sched_yield 14
#define SYS_ptrace      15
#define SYS_sigprocmask 16
#define SYS_getpriority 17
#define SYS_setpriority 18
#define SYS_getrlimit   19
#define SYS_setrlimit   20
#define SYS_getrusage   21
#define SYS_sigreturn   22
#define SYS_gettid      23
#define SYS_settls      24
#define SYS_gettls      25
#define SYS_exit_group  26
#define SYS_times       27
#define SYS_sigaltstack 28
#define SYS_sigpending  29

/* file */
#define SYS_openat         40
#define SYS_close          41
#define SYS_readv          42
#define SYS_writev         43
#define SYS_faccessat      44
#define SYS_linkat         45
#define SYS_unlinkat       46
#define SYS_chdir          47
#define SYS_mknodat        48
#define SYS_fchmodat       49
#define SYS_fchownat       50
#define SYS_fstatat        51
#define SYS_lseek          52
#define SYS_dup            53
#define SYS_pipe2          54
#define SYS_ioctl          55
#define SYS_dup3           56
#define SYS_symlinkat      57
#define SYS_readlinkat     58
#define SYS_umask          59
#define SYS_fchdir         60
#define SYS_getdents       61
#define SYS_utimensat      62
#define SYS_renameat       63
#define SYS_mount          64
#define SYS_fstatvfsat     65
#define SYS_ftruncateat    66
#define SYS_fcntl          67
#define SYS_kmload         68
#define SYS_kmunload       69
#define SYS_pselect        70
#define SYS_ppoll          71
#define SYS_fsync          72
#define SYS_fdatasync      73
#define SYS_chroot         74

/* creds */
#define SYS_getuid      80
#define SYS_getgid      81
#define SYS_geteuid     82
#define SYS_getegid     83
#define SYS_setreuid    84
#define SYS_setregid    85
#define SYS_getgroups   86
#define SYS_setgroups   87
#define SYS_setuid      88
#define SYS_setgid      89

/* net */
#define SYS_socket      100
#define SYS_bind        101
#define SYS_connect     102
#define SYS_listen      103
#define SYS_accept      104
#define SYS_recvmsg     105
#define SYS_sendmsg     106
#define SYS_getsockopt  107
#define SYS_setsockopt  108
#define SYS_getpeername 109
#define SYS_getsockname 110
#define SYS_shutdown    111
#define SYS_socketpair  112

/* misc */
#define SYS_time          130
#define SYS_mmap          131
#define SYS_munmap        132
#define SYS_mprotect      133
#define SYS_msync         134
#define SYS_nanosleep     135
#define SYS_clock_settime 136
#define SYS_clock_gettime 137
#define SYS_clock_getres  138
#define SYS_getpagesize   139
#define SYS_uname         140
#define SYS_futex         141
#define SYS_sigsuspend    142
#define SYS_madvise       143
#define SYS_reboot        144

/* uipc */
#define SYS_shmget     150
#define SYS_shmat      151
#define SYS_shmdt      152
#define SYS_shmctl     153
#define SYS_semget     154
#define SYS_semtimedop 155
#define SYS_semctl     156
#define SYS_msgget     157
#define SYS_msgsnd     158
#define SYS_msgrcv     159
#define SYS_msgctl     160

#ifdef __cplusplus
}
#endif
#endif
