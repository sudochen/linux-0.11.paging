#ifndef _TIMES_H
#define _TIMES_H

/* gettimofday returns this */
struct timeval {
	long	tv_sec;		/* seconds */
	long	tv_usec;	/* microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of dst correction */
};

#define	DST_NONE	0	/* not on dst */
#define	DST_USA		1	/* USA style dst */
#define	DST_AUST	2	/* Australian style dst */
#define	DST_WET		3	/* Western European dst */
#define	DST_MET		4	/* Middle European dst */
#define	DST_EET		5	/* Eastern European dst */
#define	DST_CAN		6	/* Canada */
#define	DST_GB		7	/* Great Britain and Eire */
#define	DST_RUM		8	/* Rumania */
#define	DST_TUR		9	/* Turkey */
#define	DST_AUSTALT	10	/* Australian style with shift in 1986 */

#define FD_SET(fd,fdsetp)	(*(fdsetp) |= (1 << (fd)))
#define FD_CLR(fd,fdsetp)	(*(fdsetp) &= ~(1 << (fd)))
#define FD_ISSET(fd,fdsetp)	((*(fdsetp) >> fd) & 1)
#define FD_ZERO(fdsetp)		(*(fdsetp) = 0)


/*
 * Names of the interval timers, and structure
 * defining a timer setting.
 */
#define	ITIMER_REAL	0
#define	ITIMER_VIRTUAL	1
#define	ITIMER_PROF	2

struct	itimerval {
	struct	timeval it_interval;	/* timer interval */
	struct	timeval it_value;	/* current value */
};


#include <sys/types.h>

struct tms {
	time_t tms_utime;
	time_t tms_stime;
	time_t tms_cutime;
	time_t tms_cstime;
};

extern time_t times(struct tms * tp);

#endif
