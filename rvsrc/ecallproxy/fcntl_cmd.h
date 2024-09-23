


/* Values for the second argument to `fcntl'.  */
#define F_DUPFD		0	/* Duplicate file descriptor.  */
#define F_GETFD		1	/* Get file descriptor flags.  */
#define F_SETFD		2	/* Set file descriptor flags.  */
#define F_GETFL		3	/* Get file status flags.  */
#define F_SETFL		4	/* Set file status flags.  */

// #ifndef __F_SETOWN
# define __F_SETOWN	8
# define __F_GETOWN	9
// #endif

#if defined __USE_UNIX98 || defined __USE_XOPEN2K8
# define F_SETOWN	__F_SETOWN /* Get owner (process receiving SIGIO).  */
# define F_GETOWN	__F_GETOWN /* Set owner (process receiving SIGIO).  */
#endif

// #ifndef __F_SETSIG
# define __F_SETSIG	10	/* Set number of signal to be sent.  */
# define __F_GETSIG	11	/* Get number of signal to be sent.  */
// #endif
// #ifndef __F_SETOWN_EX
# define __F_SETOWN_EX	15	/* Get owner (thread receiving SIGIO).  */
# define __F_GETOWN_EX	16	/* Set owner (thread receiving SIGIO).  */
// #endif

#ifdef __USE_GNU
# define F_SETSIG	__F_SETSIG	/* Set number of signal to be sent.  */
# define F_GETSIG	__F_GETSIG	/* Get number of signal to be sent.  */
# define F_SETOWN_EX	__F_SETOWN_EX	/* Get owner (thread receiving SIGIO).  */
# define F_GETOWN_EX	__F_GETOWN_EX	/* Set owner (thread receiving SIGIO).  */
#endif

#ifdef __USE_GNU
# define F_SETLEASE	1024	/* Set a lease.  */
# define F_GETLEASE	1025	/* Enquire what lease is active.  */
# define F_NOTIFY	1026	/* Request notifications on a directory.  */
# define F_SETPIPE_SZ	1031	/* Set pipe page size array.  */
# define F_GETPIPE_SZ	1032	/* Set pipe page size array.  */
# define F_ADD_SEALS	1033	/* Add seals to file.  */
# define F_GET_SEALS	1034	/* Get seals for file.  */
/* Set / get write life time hints.  */
# define F_GET_RW_HINT	1035
# define F_SET_RW_HINT	1036
# define F_GET_FILE_RW_HINT	1037
# define F_SET_FILE_RW_HINT	1038
#endif
#ifdef __USE_XOPEN2K8
# define F_DUPFD_CLOEXEC 1030	/* Duplicate file descriptor with
				   close-on-exit set.  */
#endif

/* For F_[GET|SET]FD.  */
#define FD_CLOEXEC	1	/* Actually anything with low bit set goes */

// #ifndef F_RDLCK
/* For posix fcntl() and `l_type' field of a `struct flock' for lockf().  */
# define F_RDLCK		0	/* Read lock.  */
# define F_WRLCK		1	/* Write lock.  */
# define F_UNLCK		2	/* Remove lock.  */
// #endif


/* For old implementation of BSD flock.  */
// #ifndef F_EXLCK
# define F_EXLCK		4	/* or 3 */
# define F_SHLCK		8	/* or 4 */
// #endif

// #ifdef __USE_MISC
/* Operations for BSD flock, also used by the kernel implementation.  */
# define LOCK_SH	1	/* Shared lock.  */
# define LOCK_EX	2	/* Exclusive lock.  */
# define LOCK_NB	4	/* Or'd with one of the above to prevent
				   blocking.  */
# define LOCK_UN	8	/* Remove lock.  */
// #endif

#ifdef __USE_GNU
# define LOCK_MAND	32	/* This is a mandatory flock:  */
# define LOCK_READ	64	/* ... which allows concurrent read operations.  */
# define LOCK_WRITE	128	/* ... which allows concurrent write operations.  */
# define LOCK_RW	192	/* ... Which allows concurrent read & write operations.  */
#endif

#ifdef __USE_GNU
/* Types of directory notifications that may be requested with F_NOTIFY.  */
# define DN_ACCESS	0x00000001	/* File accessed.  */
# define DN_MODIFY	0x00000002	/* File modified.  */
# define DN_CREATE	0x00000004	/* File created.  */
# define DN_DELETE	0x00000008	/* File removed.  */
# define DN_RENAME	0x00000010	/* File renamed.  */
# define DN_ATTRIB	0x00000020	/* File changed attributes.  */
# define DN_MULTISHOT	0x80000000	/* Don't remove notifier.  */
#endif


