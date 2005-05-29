
/*--------------------------------------------------------------------*/
/*--- Linux-specific stuff for the core.                           ---*/
/*---                                              linux/core_os.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2005 Nicholas Nethercote
      njn@valgrind.org

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

#ifndef __LINUX_CORE_OS_H
#define __LINUX_CORE_OS_H

/* Not really a semaphore, but use a pipe for a token-passing scheme */
typedef struct {
   Int pipe[2];
   Int owner_thread;		/* who currently has it */
} vg_sema_t;

void VGO_(sema_init)(vg_sema_t *);
void VGO_(sema_deinit)(vg_sema_t *);
void VGO_(sema_down)(vg_sema_t *sema);
void VGO_(sema_up)(vg_sema_t *sema);

/* OS-specific thread state */
typedef struct {
   /* who we are */
   Int	lwpid;			/* PID of kernel task */
   Int	threadgroup;		/* thread group id */

   ThreadId parent;		/* parent tid (if any) */

   /* runtime details */
   Addr  valgrind_stack_base;	/* Valgrind's stack base */
   SizeT valgrind_stack_szB;	/* stack size in bytes */

   /* exit details */
   Int  exitcode;		/* in the case of exitgroup, set by someone else */
   Int  fatalsig;		/* fatal signal */
} os_thread_t;

#endif   // __LINUX_CORE_OS_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
