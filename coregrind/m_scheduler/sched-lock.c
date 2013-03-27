
/*--------------------------------------------------------------------*/
/*--- Scheduler lock support functions                sched-lock.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2011 Bart Van Assche <bvanassche@acm.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "config.h"
#include "pub_core_basics.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_mallocfree.h"
#include "pub_core_libcsetjmp.h"    // To keep pub_core_threadstate.h happy
#include "pub_core_vki.h"           // To keep pub_core_threadstate.h happy
#include "pub_core_threadstate.h"
#include "priv_sema.h"
#include "priv_sched-lock.h"
#include "priv_sched-lock-impl.h"

static struct sched_lock_ops const *sched_lock_ops =
//   &ML_(generic_sched_lock_ops);
   &ML_(rwlock_sched_lock_ops); //mtV? we need a better way
// to define the scheduler policy. Probably something like
// --sched=[list of policies acceptable separated by commas]
// if a sched policy is not ok, then we try the next one.

static struct sched_lock_ops const *const sched_lock_impl[] = {
   [sched_lock_generic] = &ML_(generic_sched_lock_ops),
   [sched_lock_rwlock] = &ML_(rwlock_sched_lock_ops),
#ifdef ENABLE_LINUX_TICKET_LOCK
   [sched_lock_ticket]  = &ML_(linux_ticket_lock_ops),
#endif
};

/**
 * Define which scheduler lock implementation to use.
 *
 * @param[in] t Scheduler lock type.
 *
 * @return True if and only if this function succeeded.
 *
 * @note Must be called before any other sched_lock*() function is invoked.
 */
Bool ML_(set_sched_lock_impl)(const enum SchedLockType t)
{
   struct sched_lock_ops const *p = NULL;

   if ((unsigned)t < sizeof(sched_lock_impl)/sizeof(sched_lock_impl[0]))
      p = sched_lock_impl[t];
   if (p)
      sched_lock_ops = p;
   return !!p;
}

const HChar *ML_(get_sched_lock_name)(void)
{
   return (sched_lock_ops->get_sched_lock_name)();
}

struct sched_lock *ML_(create_sched_lock)(void)
{
   return (sched_lock_ops->create_sched_lock)();
}

void ML_(destroy_sched_lock)(struct sched_lock *p)
{
   return (sched_lock_ops->destroy_sched_lock)(p);
}

int ML_(get_sched_lock_owner)(struct sched_lock *p)
{
   return (sched_lock_ops->get_sched_lock_owner)(p);
}

void ML_(acquire_sched_lock)(struct sched_lock *p, ThreadId tid, SchedLockKind slk)
{
   return (sched_lock_ops->acquire_sched_lock)(p, tid, slk);
}

void ML_(release_sched_lock)(struct sched_lock *p, ThreadId tid, SchedLockKind slk)
{
   return (sched_lock_ops->release_sched_lock)(p, tid, slk);
}
