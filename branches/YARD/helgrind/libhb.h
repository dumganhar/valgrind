
/*--------------------------------------------------------------------*/
/*--- Public interface for libhb.                          libhb.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of LibHB, a library for implementing and checking
   the happens-before relationship in concurrent programs.

   Copyright (C) 2008-2008 OpenWorks Ltd
      info@open-works.co.uk

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

#ifndef __LIBHB_H
#define __LIBHB_H

/* Abstract to user: thread identifiers */
/* typedef  struct _Thr  Thr; */ /* now in hg_lock_n_thread.h */

/* Abstract to user: synchronisation objects */
/* typedef  struct _SO  SO; */ /* now in hg_lock_n_thread.h */

/* Abstract to the lib: execution contexts */
/* struct _EC will be defined by user at some point. */
typedef  struct _EC  EC;

/* Concrete to user: info on races.  tidp and wherep are the previous
   (other) access in the race. */
typedef
   struct { Thr* thr;  struct EC_* where; Addr a; SizeT szB; Bool isW;
            Thr* thrp; struct EC_* wherep; }
   RaceInfo;

/* Initialise library; returns Thr* for root thread.  'shadow_alloc'
   should never return NULL, instead it should simply not return if
   they encounter an out-of-memory condition. */
Thr* libhb_init (
        void        (*get_stacktrace)( Thr*, Addr*, UWord ),
        struct EC_* (*stacktrace_to_EC)( Addr*, UWord ),
        struct EC_* (*get_EC)( Thr* )
     );

/* Shut down the library, and print stats (in fact that's _all_
   this is for.) */
void libhb_shutdown ( Bool show_stats );

/* Thread creation: returns Thr* for new thread */
Thr* libhb_create ( Thr* parent );

/* Thread async exit */
void libhb_async_exit ( Thr* exitter );

/* Synchronisation objects (abstract to caller) */

/* Allocate a new one (alloc'd by library) */
SO* libhb_so_alloc ( void );

/* Dealloc one */
void libhb_so_dealloc ( SO* so );

/* Send a message via a sync object.  If strong_send is true, the
   resulting inter-thread dependency seen by a future receiver of this
   message will be a dependency on this thread only.  That is, in a
   strong send, the VC inside the SO is replaced by the clock of the
   sending thread.  For a weak send, the sender's VC is joined into
   that already in the SO, if any.  This subtlety is needed to model
   rwlocks: a strong send corresponds to releasing a rwlock that had
   been w-held (or releasing a standard mutex).  A weak send
   corresponds to releasing a rwlock that has been r-held.

   (rationale): Since in general many threads may hold a rwlock in
   r-mode, a weak send facility is necessary in order that the final
   SO reflects the join of the VCs of all the threads releasing the
   rwlock, rather than merely holding the VC of the most recent thread
   to release it. */
void libhb_so_send ( Thr* thr, SO* so, Bool strong_send );

/* Recv a message from a sync object.  If strong_recv is True, the
   resulting inter-thread dependency is considered adequate to induce
   a h-b ordering on both reads and writes.  If it is False, the
   implied h-b ordering exists only for reads, not writes.  This is
   subtlety is required in order to support reader-writer locks: a
   thread doing a write-acquire of a rwlock (or acquiring a normal
   mutex) models this by doing a strong receive.  A thread doing a
   read-acquire of a rwlock models this by doing a !strong_recv. */
void libhb_so_recv ( Thr* thr, SO* so, Bool strong_recv );

/* Has this SO ever been sent on? */
Bool libhb_so_everSent ( SO* so );

/* Memory accesses (1/2/4/8 byte size).  Returns True if this access
   resulted in a reportable race, in which case the details are placed
   in *ri.  If False is returned, the contents of *ri are unspecified
   and should not be consulted. */
Bool libhb_write ( /*OUT*/RaceInfo* ri, Thr* thr, Addr a, SizeT szB );
Bool libhb_read  ( /*OUT*/RaceInfo* ri, Thr* thr, Addr a, SizeT szB );

/* Set memory address ranges to new (freshly allocated), or noaccess
   (no longer accessible). */
void libhb_range_new      ( Thr*, Addr, SizeT );
void libhb_range_noaccess ( Thr*, Addr, SizeT );

/* For the convenience of callers, we offer to store one void* item in
   a Thr, which we ignore, but the caller can get or set any time. */
void* libhb_get_Thr_opaque ( Thr* );
void  libhb_set_Thr_opaque ( Thr*, void* );

/* Low level copy of shadow state from [src,src+len) to [dst,dst+len).
   Overlapping moves are checked for and asserted against. */
void libhb_copy_shadow_state ( Addr src, Addr dst, SizeT len );

/* Call this periodically to give libhb the opportunity to
   garbage-collect its internal data structures. */
void libhb_maybe_GC ( void );

#endif /* __LIBHB_H */

/*--------------------------------------------------------------------*/
/*--- end                                                  libhb.h ---*/
/*--------------------------------------------------------------------*/
