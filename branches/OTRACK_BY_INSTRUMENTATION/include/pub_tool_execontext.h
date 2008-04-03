/*--------------------------------------------------------------------*/
/*--- ExeContexts: long-lived stack traces.  pub_tool_execontext.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2008 Julian Seward
      jseward@acm.org

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

#ifndef __PUB_TOOL_EXECONTEXT_H
#define __PUB_TOOL_EXECONTEXT_H

// It's an abstract type.
typedef
   struct _ExeContext
   ExeContext;

// Resolution type used to decide how closely to compare two errors for
// equality.
typedef
   enum { Vg_LowRes, Vg_MedRes, Vg_HighRes }
   VgRes;

// Take a snapshot of the client's stack.  Search our collection of
// ExeContexts to see if we already have it, and if not, allocate a
// new one.  Either way, return a pointer to the context.  Context size
// controlled by --num-callers option.
//
// This should only be used for long-lived stack traces.  If you want a
// short-lived stack trace, use VG_(get_StackTrace)().
//
// If called from generated code, use VG_(get_running_tid)() to get the
// current ThreadId.  If called from non-generated code, the current
// ThreadId should be passed in by the core.  The initial IP value to 
// use is adjusted by first_ip_delta before the stack is unwound.
// A safe value to pass is zero.
extern 
ExeContext* VG_(record_ExeContext) ( ThreadId tid, Word first_ip_delta );

// Trivial version of VG_(record_ExeContext), which just records the
// thread's current program counter but does not do any stack
// unwinding.  This is useful in some rare cases when we suspect the
// stack might be outside mapped storage, and so unwinding
// might cause a segfault.  In this case we can at least safely
// produce a one-element stack trace, which is better than nothing.
extern
ExeContext* VG_(record_depth_1_ExeContext)( ThreadId tid );

// Apply a function to every element in the ExeContext.  The parameter 'n'
// gives the index of the passed ip.  Doesn't go below main() unless
// --show-below-main=yes is set.
extern void VG_(apply_ExeContext)( void(*action)(UInt n, Addr ip),
                                   ExeContext* ec, UInt n_ips );

// Compare two ExeContexts.  Number of callers considered depends on `res':
//   Vg_LowRes:  2
//   Vg_MedRes:  4
//   Vg_HighRes: all
extern Bool VG_(eq_ExeContext) ( VgRes res, ExeContext* e1, ExeContext* e2 );

// Print an ExeContext.
extern void VG_(pp_ExeContext) ( ExeContext* ec );

// Get the 32-bit unique reference number for this ExeContext.
// Guaranteed to be nonzero.
extern UInt VG_(get_ExeContext_uniq)( ExeContext* e );

// How many entries (frames) in this ExeContext?
extern Int VG_(get_ExeContext_n_ips)( ExeContext* e );

// Find the ExeContext that has the given uniq, if any
extern ExeContext* VG_(get_ExeContext_from_uniq)( UInt uniq );

// Make an ExeContext containing just 'a', and nothing else
ExeContext* VG_(make_depth_1_ExeContext_from_Addr)( Addr a );


#endif   // __PUB_TOOL_EXECONTEXT_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
