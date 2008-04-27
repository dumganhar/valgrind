/** Test Valgrind's interception of the Linux syscalls timerfd_create(),
 *  timerfd_gettime() and timerfd_settime().
 *
 *  This is a modified version of
 *  timerfd-test2 by Davide Libenzi (test app for timerfd)
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *  Modified for inclusion in Valgrind.
 *  Copyright (C) 2008  Bart Van Assche <bart.vanassche@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  See also http://www.xmailserver.org/timerfd-test2.c
 */

#if defined(__linux__)

#define _GNU_SOURCE

#include "../../config.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(HAVE_SYS_SIGNAL_H)
#include <sys/signal.h>
#endif
#if defined(HAVE_SYS_SYSCALL_H)
#include <sys/syscall.h>
#endif
#if defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#endif
#if defined(HAVE_SYS_TYPES_H)
#include <sys/types.h>
#endif


/*
 * timerfd_* system call numbers introduced in 2.6.23. These constants are
 * not yet in the glibc 2.7 headers, that is why they are defined here.
 */
#ifndef __NR_timerfd_create
#if defined(__x86_64__)
#define __NR_timerfd_create  283
#define __NR_timerfd_settime 286
#define __NR_timerfd_gettime 287
#elif defined(__i386__)
#define __NR_timerfd_create  322
#define __NR_timerfd_settime 325
#define __NR_timerfd_gettime 326
#elif defined(__powerpc__)
#define __NR_timerfd_create  306
#define __NR_timerfd_settime 311
#define __NR_timerfd_gettime 312
#else
#error Cannot detect your architecture!
#endif
#endif



/* Definitions from include/linux/timerfd.h */
#define TFD_TIMER_ABSTIME (1 << 0)



struct tmr_type
{
  int id;
  char const *name;
};


#if defined(HAVE_CLOCK_GETTIME)
unsigned long long getustime(int clockid)
{
  struct timespec tp;

  if (clock_gettime((clockid_t) clockid, &tp))
  {
    perror("clock_gettime");
    return 0;
  }

  return 1000000ULL * tp.tv_sec + tp.tv_nsec / 1000;
}
#else
unsigned long long getustime(int clockid)
{
  fprintf(stderr, "error: clock_gettime\n");
  return 0;
}
#endif

void set_timespec(struct timespec *tmr, unsigned long long ustime)
{
  tmr->tv_sec = (time_t) (ustime / 1000000ULL);
  tmr->tv_nsec = (long) (1000ULL * (ustime % 1000000ULL));
}

int timerfd_create(int clockid, int flags)
{
  return syscall(__NR_timerfd_create, clockid, flags);
}

int timerfd_settime(int ufc, int flags, const struct itimerspec *utmr,
		    struct itimerspec *otmr)
{
  return syscall(__NR_timerfd_settime, ufc, flags, utmr, otmr);
}

int timerfd_gettime(int ufc, struct itimerspec *otmr)
{
  return syscall(__NR_timerfd_gettime, ufc, otmr);
}

long waittmr(int tfd, int timeo)
{
  u_int64_t ticks;
  struct pollfd pfd;

  pfd.fd = tfd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  if (poll(&pfd, 1, timeo) < 0)
  {
    perror("poll");
    return -1;
  }
  if ((pfd.revents & POLLIN) == 0)
  {
    fprintf(stdout, "no ticks happened\n");
    return -1;
  }
  if (read(tfd, &ticks, sizeof(ticks)) != sizeof(ticks))
  {
    perror("timerfd read");
    return -1;
  }

  return ticks;
}

int main(int ac, char **av)
{
  int i, tfd;
  long ticks;
  unsigned long long tnow, ttmr;
  u_int64_t uticks;
  struct itimerspec tmr;
  struct tmr_type clks[] =
  {
    { CLOCK_MONOTONIC, "CLOCK MONOTONIC" },
    { CLOCK_REALTIME, "CLOCK REALTIME" },
  };

  for (i = 0; i < sizeof(clks) / sizeof(clks[0]); i++)
  {
    fprintf(stdout, "\n\n---------------------------------------\n");
    fprintf(stdout, "| testing %s\n", clks[i].name);
    fprintf(stdout, "---------------------------------------\n\n");

    fprintf(stdout, "relative timer test (at 500 ms) ...\n");
    set_timespec(&tmr.it_value, 500 * 1000);
    set_timespec(&tmr.it_interval, 0);
    tnow = getustime(clks[i].id);
    if ((tfd = timerfd_create(clks[i].id, 0)) == -1)
    {
      perror("timerfd_create");
      return 1;
    }
    fprintf(stdout, "timerfd = %d\n", tfd);

    if (timerfd_settime(tfd, 0, &tmr, NULL))
    {
      perror("timerfd_settime");
      return 1;
    }

    fprintf(stdout, "wating timer ...\n");
    ticks = waittmr(tfd, -1);
    ttmr = getustime(clks[i].id);
    if (ticks <= 0)
      fprintf(stdout, "whooops! no timer showed up!\n");
    else
      fprintf(stdout, "got timer ticks (%ld) after %.1f s\n",
              ticks, (ttmr - tnow) * 1e-6);


    fprintf(stdout, "absolute timer test (at 500 ms) ...\n");
    tnow = getustime(clks[i].id);
    set_timespec(&tmr.it_value, tnow + 500 * 1000);
    set_timespec(&tmr.it_interval, 0);
    if (timerfd_settime(tfd, TFD_TIMER_ABSTIME, &tmr, NULL))
    {
      perror("timerfd_settime");
      return 1;
    }

    fprintf(stdout, "wating timer ...\n");
    ticks = waittmr(tfd, -1);
    ttmr = getustime(clks[i].id);
    if (ticks <= 0)
      fprintf(stdout, "whooops! no timer showed up!\n");
    else
      fprintf(stdout, "got timer ticks (%ld) after %.1f s\n",
              ticks, (ttmr - tnow) * 1e-6);

    fprintf(stdout, "sequential timer test (100 ms clock) ...\n");
    tnow = getustime(clks[i].id);
    set_timespec(&tmr.it_value, tnow + 100 * 1000);
    set_timespec(&tmr.it_interval, 100 * 1000);
    if (timerfd_settime(tfd, TFD_TIMER_ABSTIME, &tmr, NULL))
    {
      perror("timerfd_settime");
      return 1;
    }

    fprintf(stdout, "sleeping one second ...\n");
    sleep(1);
    if (timerfd_gettime(tfd, &tmr))
    {
      perror("timerfd_gettime");
      return 1;
    }
    fprintf(stdout, "timerfd_gettime returned:\n"
            "\tit_value = %.1f it_interval = %.1f\n",
            tmr.it_value.tv_sec + 1e-9 * tmr.it_value.tv_nsec,
            tmr.it_interval.tv_sec + 1e-9 * tmr.it_interval.tv_nsec);
    fprintf(stdout, "sleeping 1 second ...\n");
    sleep(1);

    fprintf(stdout, "wating timer ...\n");
    ticks = waittmr(tfd, -1);
    ttmr = getustime(clks[i].id);
    if (ticks <= 0)
      fprintf(stdout, "whooops! no timer showed up!\n");
    else
      fprintf(stdout, "got timer ticks (%ld) after %.1f s\n",
              ticks, (ttmr - tnow) * 1e-6);


    fprintf(stdout, "O_NONBLOCK test ...\n");
    tnow = getustime(clks[i].id);
    set_timespec(&tmr.it_value, 100 * 1000);
    set_timespec(&tmr.it_interval, 0);
    if (timerfd_settime(tfd, 0, &tmr, NULL))
    {
      perror("timerfd_settime");
      return 1;
    }
    fprintf(stdout, "timerfd = %d\n", tfd);

    fprintf(stdout, "wating timer (flush the single tick) ...\n");
    ticks = waittmr(tfd, -1);
    ttmr = getustime(clks[i].id);
    if (ticks <= 0)
      fprintf(stdout, "whooops! no timer showed up!\n");
    else
      fprintf(stdout, "got timer ticks (%ld) after %.1f s\n",
              ticks, (ttmr - tnow) * 1e-6);

    fcntl(tfd, F_SETFL, fcntl(tfd, F_GETFL, 0) | O_NONBLOCK);

    if (read(tfd, &uticks, sizeof(uticks)) > 0)
      fprintf(stdout, "whooops! timer ticks not zero when should have been\n");
    else if (errno != EAGAIN)
      fprintf(stdout, "whooops! bad errno value (%d = '%s')!\n",
              errno, strerror(errno));
    else
      fprintf(stdout, "success\n");

    fcntl(tfd, F_SETFL, fcntl(tfd, F_GETFL, 0) & ~O_NONBLOCK);

    close(tfd);
  }

  return 0;
}

#endif /* __linux__ */
