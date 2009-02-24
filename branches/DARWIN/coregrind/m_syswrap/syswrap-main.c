
/*--------------------------------------------------------------------*/
/*--- Handle system calls.                          syswrap-main.c ---*/
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

#include "libvex_guest_offsets.h"
#include "libvex_trc_values.h"
#include "pub_core_basics.h"
#include "pub_core_aspacemgr.h"
#include "pub_core_vki.h"
#include "pub_core_vkiscnums.h"
#include "pub_core_threadstate.h"
#include "pub_core_libcbase.h"
#include "pub_core_libcassert.h"
#include "pub_core_libcprint.h"
#include "pub_core_libcproc.h"      // For VG_(getpid)()
#include "pub_core_libcsignal.h"
#include "pub_core_scheduler.h"     // For VG_({acquire,release}_BigLock),
                                    //   and VG_(vg_yield)
#include "pub_core_stacktrace.h"    // For VG_(get_and_pp_StackTrace)()
#include "pub_core_tooliface.h"
#include "pub_core_options.h"
#include "pub_core_signals.h"       // For VG_SIGVGKILL, VG_(poll_signals)
#include "pub_core_syscall.h"
#include "pub_core_machine.h"
#include "pub_core_syswrap.h"

#include "priv_types_n_macros.h"
#include "priv_syswrap-main.h"

#if defined(VGO_darwin)
#include "priv_syswrap-darwin.h"
#endif

/* Useful info which needs to be recorded somewhere:
   Use of registers in syscalls is:

          NUM ARG1 ARG2 ARG3 ARG4 ARG5 ARG6 ARG7 ARG8 RESULT
   LINUX:
   x86    eax ebx  ecx  edx  esi  edi  ebp  n/a  n/a  eax       (== NUM)
   amd64  rax rdi  rsi  rdx  r10  r8   r9   n/a  n/a  rax       (== NUM)
   ppc32  r0  r3   r4   r5   r6   r7   r8   n/a  n/a  r3+CR0.SO (== ARG1)
   ppc64  r0  r3   r4   r5   r6   r7   r8   n/a  n/a  r3+CR0.SO (== ARG1)

   AIX:
   ppc32  r2  r3   r4   r5   r6   r7   r8   r9   r10  r3(res),r4(err)
   ppc64  r2  r3   r4   r5   r6   r7   r8   r9   r10  r3(res),r4(err)

   DARWIN:
   ppc32  r0  r3   r4   r5   r6   r7   r8   r9   r10  r3+r4+pc
          BSD syscalls: result is in r3/r4, with LSB in r3
          on failure, execution returns to the instruction following `sc`
          on success, execution returns to the 2nd instruction following `sc`
          Mach traps: result is in r3, and there is no error flag.
   ppc64  r0  r3   r4   r5   r6   r7   r8   ??   ??   r3+CR0.SO (== ARG1)
   x86    stk stk  stk  stk  stk  stk  stk  stk  stk  eax+edx+cc
   amd64  raw rdi  rsi  rdx  rcx  r8   r9   stk  stk  eax+edx+cc
*/

/* This is the top level of the system-call handler module.  All
   system calls are channelled through here, doing two things:

   * notify the tool of the events (mem/reg reads, writes) happening

   * perform the syscall, usually by passing it along to the kernel
     unmodified.

   A magical piece of assembly code, do_syscall_for_client_WRK, in
   syscall-$PLATFORM.S does the tricky bit of passing a syscall to the
   kernel, whilst having the simulator retain control.
*/

/* The main function is VG_(client_syscall).  The simulation calls it
   whenever a client thread wants to do a syscall.  The following is a
   sketch of what it does.

   * Ensures the root thread's stack is suitably mapped.  Tedious and
     arcane.  See big big comment in VG_(client_syscall).

   * First, it rounds up the syscall number and args (which is a
     platform dependent activity) and puts them in a struct ("args")
     and also a copy in "orig_args".

     The pre/post wrappers refer to these structs and so no longer
     need magic macros to access any specific registers.  This struct
     is stored in thread-specific storage.


   * The pre-wrapper is called, passing it a pointer to struct
     "args".


   * The pre-wrapper examines the args and pokes the tool
     appropriately.  It may modify the args; this is why "orig_args"
     is also stored.

     The pre-wrapper may choose to 'do' the syscall itself, and
     concludes one of three outcomes:

       Success(N)    -- syscall is already complete, with success;
                        result is N

       Fail(N)       -- syscall is already complete, with failure;
                        error code is N

       HandToKernel  -- (the usual case): this needs to be given to
                        the kernel to be done, using the values in
                        the possibly-modified "args" struct.

     In addition, the pre-wrapper may set some flags:

       MayBlock   -- only applicable when outcome==HandToKernel

       PostOnFail -- only applicable when outcome==HandToKernel or Fail


   * If the pre-outcome is HandToKernel, the syscall is duly handed
     off to the kernel (perhaps involving some thread switchery, but
     that's not important).  This reduces the possible set of outcomes
     to either Success(N) or Fail(N).


   * The outcome (Success(N) or Fail(N)) is written back to the guest
     register(s).  This is platform specific:

     x86:    Success(N) ==>  eax = N
             Fail(N)    ==>  eax = -N

     ditto amd64

     ppc32:  Success(N) ==>  r3 = N, CR0.SO = 0
             Fail(N) ==>     r3 = N, CR0.SO = 1

     Darwin:
     ppc32:  Success(N) ==>  r3 = N, pc = LAST_SC+8
             Fail(N)    ==>  r3 = N, pc = LAST_SC+4
     x86:    Success(N) ==>  eax,edx = N, cc = 0
             Fail(N)    ==>  eax,edx = N, cc = 1

   * The post wrapper is called if:

     - it exists, and
     - outcome==Success or (outcome==Fail and PostOnFail is set)

     The post wrapper is passed the adulterated syscall args (struct
     "args"), and the syscall outcome (viz, Success(N) or Fail(N)).

   There are several other complications, primarily to do with
   syscalls getting interrupted, explained in comments in the code.
*/

/* CAVEATS for writing wrappers.  It is important to follow these!

   The macros defined in priv_types_n_macros.h are designed to help
   decouple the wrapper logic from the actual representation of
   syscall args/results, since these wrappers are designed to work on
   multiple platforms.

   Sometimes a PRE wrapper will complete the syscall itself, without
   handing it to the kernel.  It will use one of SET_STATUS_Success,
   SET_STATUS_Failure or SET_STATUS_from_SysRes to set the return
   value.  It is critical to appreciate that use of the macro does not
   immediately cause the underlying guest state to be updated -- that
   is done by the driver logic in this file, when the wrapper returns.

   As a result, PRE wrappers of the following form will malfunction:

   PRE(fooble) 
   {
      ... do stuff ...
      SET_STATUS_Somehow(...)

      // do something that assumes guest state is up to date
   }

   In particular, direct or indirect calls to VG_(poll_signals) after
   setting STATUS can cause the guest state to be read (in order to
   build signal frames).  Do not do this.  If you want a signal poll
   after the syscall goes through, do "*flags |= SfPollAfter" and the
   driver logic will do it for you.

   -----------

   Another critical requirement following introduction of new address
   space manager (JRS, 20050923):

   In a situation where the mappedness of memory has changed, aspacem
   should be notified BEFORE the tool.  Hence the following is
   correct:

      Bool d = VG_(am_notify_munmap)(s->start, s->end+1 - s->start);
      VG_TRACK( die_mem_munmap, s->start, s->end+1 - s->start );
      if (d)
         VG_(discard_translations)(s->start, s->end+1 - s->start);

   whilst this is wrong:

      VG_TRACK( die_mem_munmap, s->start, s->end+1 - s->start );
      Bool d = VG_(am_notify_munmap)(s->start, s->end+1 - s->start);
      if (d)
         VG_(discard_translations)(s->start, s->end+1 - s->start);

   The reason is that the tool may itself ask aspacem for more shadow
   memory as a result of the VG_TRACK call.  In such a situation it is
   critical that aspacem's segment array is up to date -- hence the
   need to notify aspacem first.

   -----------

   Also .. take care to call VG_(discard_translations) whenever
   memory with execute permissions is unmapped.
*/


/* ---------------------------------------------------------------------
   Do potentially blocking syscall for the client, and mess with 
   signal masks at the same time. 
   ------------------------------------------------------------------ */

/* Perform a syscall on behalf of a client thread, using a specific
   signal mask.  On completion, the signal mask is set to restore_mask
   (which presumably blocks almost everything).  If a signal happens
   during the syscall, the handler should call
   VG_(fixup_guest_state_after_syscall_interrupted) to adjust the
   thread's context to do the right thing.

   The _WRK function is handwritten assembly, implemented per-platform
   in coregrind/m_syswrap/syscall-$PLAT.S.  It has some very magic
   properties.  See comments at the top of
   VG_(fixup_guest_state_after_syscall_interrupted) below for details.
*/
#if defined(VGO_darwin)
extern
UWord ML_(do_syscall_for_client_unix_WRK)( Word syscallno, 
                                           void* guest_state,
                                           const vki_sigset_t *syscall_mask,
                                           const vki_sigset_t *restore_mask,
                                           Word nsigwords);
extern
UWord ML_(do_syscall_for_client_ux64_WRK)( Word syscallno, 
                                           void* guest_state,
                                           const vki_sigset_t *syscall_mask,
                                           const vki_sigset_t *restore_mask,
                                           Word nsigwords);
extern
UWord ML_(do_syscall_for_client_mach_WRK)( Word syscallno, 
                                           void* guest_state,
                                           const vki_sigset_t *syscall_mask,
                                           const vki_sigset_t *restore_mask,
                                           Word nsigwords);
extern
UWord ML_(do_syscall_for_client_mdep_WRK)( Word syscallno, 
                                           void* guest_state,
                                           const vki_sigset_t *syscall_mask,
                                           const vki_sigset_t *restore_mask,
                                           Word nsigwords);
#endif

extern
UWord ML_(do_syscall_for_client_WRK)( Word syscallno, 
                                      void* guest_state,
                                      const vki_sigset_t *syscall_mask,
                                      const vki_sigset_t *restore_mask,
                                      Word nsigwords
#                                     if defined(VGO_aix5)
                                      , Word __nr_sigprocmask
#                                     endif
                                    );

static
void do_syscall_for_client ( Int syscallno,
                             ThreadState* tst,
                             const vki_sigset_t* syscall_mask )
{
   vki_sigset_t saved;
   UWord err 
      = ML_(do_syscall_for_client_WRK)(
           syscallno, &tst->arch.vex, 
           syscall_mask, &saved, sizeof(vki_sigset_t) // GrP fixme correct? _VKI_NSIG_WORDS * sizeof(UWord)
#          if defined(VGO_aix5)
           , __NR_rt_sigprocmask
#          endif
        );
   vg_assert2(
      err == 0,
      "ML_(do_syscall_for_client_WRK): sigprocmask error %d",
      (Int)(err & 0xFFF)
   );
}


#if defined(VGO_darwin)
extern
UWord ML_(do_syscall_for_client_WRK)( Word syscallno, 
                                      void* guest_state,
                                      const vki_sigset_t *syscall_mask,
                                      const vki_sigset_t *restore_mask,
                                      Word nsigwords)
{
   switch (VG_DARWIN_SYSNO_CLASS(syscallno)) {
   case VG_DARWIN_SYSCALL_CLASS_UNIX:
      return ML_(do_syscall_for_client_unix_WRK)
         (VG_DARWIN_SYSNO_NUM(syscallno), guest_state, 
          syscall_mask, restore_mask, nsigwords);
   case VG_DARWIN_SYSCALL_CLASS_UX64:
      return ML_(do_syscall_for_client_ux64_WRK)
         (VG_DARWIN_SYSNO_NUM(syscallno), guest_state, 
          syscall_mask, restore_mask, nsigwords);
   case VG_DARWIN_SYSCALL_CLASS_MACH:
      return ML_(do_syscall_for_client_mach_WRK)
         (VG_DARWIN_SYSNO_NUM(syscallno), guest_state, 
          syscall_mask, restore_mask, nsigwords);
   case VG_DARWIN_SYSCALL_CLASS_MDEP:
      return ML_(do_syscall_for_client_mdep_WRK)
         (VG_DARWIN_SYSNO_NUM(syscallno), guest_state, 
          syscall_mask, restore_mask, nsigwords);
   default:
      vg_assert(0);
      return 0;
   }
}
#endif


/* ---------------------------------------------------------------------
   Impedance matchers and misc helpers
   ------------------------------------------------------------------ */

static
Bool eq_SyscallArgs ( SyscallArgs* a1, SyscallArgs* a2 )
{
   return a1->sysno == a2->sysno
          && a1->arg1 == a2->arg1
          && a1->arg2 == a2->arg2
          && a1->arg3 == a2->arg3
          && a1->arg4 == a2->arg4
          && a1->arg5 == a2->arg5
          && a1->arg6 == a2->arg6
          && a1->arg7 == a2->arg7
          && a1->arg8 == a2->arg8;
}

static
Bool eq_SyscallStatus ( SyscallStatus* s1, SyscallStatus* s2 )
{
   return s1->what == s2->what 
          && s1->sres.res == s2->sres.res
          && s1->sres.err == s2->sres.err;
}


/* Convert between SysRes and SyscallStatus, to the extent possible. */

static
SyscallStatus convert_SysRes_to_SyscallStatus ( SysRes res )
{
   SyscallStatus status;
   status.what = SsComplete;
   status.sres = res;
   return status;
}


/* Impedance matchers.  These convert syscall arg or result data from
   the platform-specific in-guest-state format to the canonical
   formats, and back. */

static 
void getSyscallArgsFromGuestState ( /*OUT*/SyscallArgs*       canonical,
                                    /*IN*/ VexGuestArchState* gst_vanilla, 
                                    /*IN*/ UInt trc )
{
#if defined(VGP_x86_linux)
   VexGuestX86State* gst = (VexGuestX86State*)gst_vanilla;
   canonical->sysno = gst->guest_EAX;
   canonical->arg1  = gst->guest_EBX;
   canonical->arg2  = gst->guest_ECX;
   canonical->arg3  = gst->guest_EDX;
   canonical->arg4  = gst->guest_ESI;
   canonical->arg5  = gst->guest_EDI;
   canonical->arg6  = gst->guest_EBP;
   canonical->arg7  = 0;
   canonical->arg8  = 0;

#elif defined(VGP_amd64_linux)
   VexGuestAMD64State* gst = (VexGuestAMD64State*)gst_vanilla;
   canonical->sysno = gst->guest_RAX;
   canonical->arg1  = gst->guest_RDI;
   canonical->arg2  = gst->guest_RSI;
   canonical->arg3  = gst->guest_RDX;
   canonical->arg4  = gst->guest_R10;
   canonical->arg5  = gst->guest_R8;
   canonical->arg6  = gst->guest_R9;
   canonical->arg7  = 0;
   canonical->arg8  = 0;


#elif defined(VGP_ppc32_linux)
   VexGuestPPC32State* gst = (VexGuestPPC32State*)gst_vanilla;
   canonical->sysno = gst->guest_GPR0;
   canonical->arg1  = gst->guest_GPR3;
   canonical->arg2  = gst->guest_GPR4;
   canonical->arg3  = gst->guest_GPR5;
   canonical->arg4  = gst->guest_GPR6;
   canonical->arg5  = gst->guest_GPR7;
   canonical->arg6  = gst->guest_GPR8;
   canonical->arg7  = 0;
   canonical->arg8  = 0;


#elif defined(VGP_ppc64_linux)
   VexGuestPPC64State* gst = (VexGuestPPC64State*)gst_vanilla;
   canonical->sysno = gst->guest_GPR0;
   canonical->arg1  = gst->guest_GPR3;
   canonical->arg2  = gst->guest_GPR4;
   canonical->arg3  = gst->guest_GPR5;
   canonical->arg4  = gst->guest_GPR6;
   canonical->arg5  = gst->guest_GPR7;
   canonical->arg6  = gst->guest_GPR8;
   canonical->arg7  = 0;
   canonical->arg8  = 0;


#elif defined(VGP_ppc32_aix5)
   VexGuestPPC32State* gst = (VexGuestPPC32State*)gst_vanilla;
   canonical->sysno = gst->guest_GPR2;
   canonical->arg1  = gst->guest_GPR3;
   canonical->arg2  = gst->guest_GPR4;
   canonical->arg3  = gst->guest_GPR5;
   canonical->arg4  = gst->guest_GPR6;
   canonical->arg5  = gst->guest_GPR7;
   canonical->arg6  = gst->guest_GPR8;
   canonical->arg7  = gst->guest_GPR9;
   canonical->arg8  = gst->guest_GPR10;

#elif defined(VGP_ppc64_aix5)
   VexGuestPPC64State* gst = (VexGuestPPC64State*)gst_vanilla;
   canonical->sysno = gst->guest_GPR2;
   canonical->arg1  = gst->guest_GPR3;
   canonical->arg2  = gst->guest_GPR4;
   canonical->arg3  = gst->guest_GPR5;
   canonical->arg4  = gst->guest_GPR6;
   canonical->arg5  = gst->guest_GPR7;
   canonical->arg6  = gst->guest_GPR8;
   canonical->arg7  = gst->guest_GPR9;
   canonical->arg8  = gst->guest_GPR10;

#elif defined(VGP_x86_darwin)
   VexGuestX86State* gst = (VexGuestX86State*)gst_vanilla;
   UWord *stack = (UWord *)gst->guest_ESP;
   // GrP fixme hope syscalls aren't called with really shallow stacks...
   canonical->sysno = gst->guest_EAX;

   if (canonical->sysno != 0) {
      // stack[0] is return address
      canonical->arg1  = stack[1];
      canonical->arg2  = stack[2];
      canonical->arg3  = stack[3];
      canonical->arg4  = stack[4];
      canonical->arg5  = stack[5];
      canonical->arg6  = stack[6];
      canonical->arg7  = stack[7];
      canonical->arg8  = stack[8];
   } else {
      // GrP fixme hack handle syscall()
      // GrP fixme what about __syscall() ?
      // stack[0] is return address
      canonical->sysno = stack[1];
      vg_assert(canonical->sysno != 0);
      canonical->arg1  = stack[2];
      canonical->arg2  = stack[3];
      canonical->arg3  = stack[4];
      canonical->arg4  = stack[5];
      canonical->arg5  = stack[6];
      canonical->arg6  = stack[7];
      canonical->arg7  = stack[8];
      canonical->arg8  = stack[9];
      
      PRINT("SYSCALL[%d,?](%5lld) syscall(#%ld, ...); please stand by...\n",
            VG_(getpid)(), /*tid,*/ (Long)0, canonical->sysno);
   }

   switch (trc) {
   case VEX_TRC_JMP_SYS_INT128:
      // int $0x80 = Unix, 64-bit result
      vg_assert(canonical->sysno >= 0);
      canonical->sysno = VG_DARWIN_SYSCALL_CONSTRUCT_UX64(canonical->sysno);
      break;
   case VEX_TRC_JMP_SYS_SYSENTER:
      // syscall = Unix, 32-bit result
      // OR        Mach, 32-bit result
      if (canonical->sysno >= 0) {
         // fixme hack  I386_SYSCALL_NUMBER_MASK
         canonical->sysno = VG_DARWIN_SYSCALL_CONSTRUCT_UNIX(canonical->sysno & 0xffff);
      } else {
         canonical->sysno = VG_DARWIN_SYSCALL_CONSTRUCT_MACH(-canonical->sysno);
      }
      break;
   case VEX_TRC_JMP_SYS_INT129:
      // int $0x81 = Mach, 32-bit result
      vg_assert(canonical->sysno < 0);
      canonical->sysno = VG_DARWIN_SYSCALL_CONSTRUCT_MACH(-canonical->sysno);
      break;
   case VEX_TRC_JMP_SYS_INT130:
      // int $0x82 = mdep, 32-bit result
      vg_assert(canonical->sysno >= 0);
      canonical->sysno = VG_DARWIN_SYSCALL_CONSTRUCT_MDEP(canonical->sysno);
      break;
   default: 
      vg_assert(0);
      break;
   }
   
#elif defined(VGP_amd64_darwin)
   VexGuestAMD64State* gst = (VexGuestAMD64State*)gst_vanilla;
   UWord *stack = (UWord *)gst->guest_RSP;

   vg_assert(trc == VEX_TRC_JMP_SYS_SYSCALL);

   // GrP fixme hope syscalls aren't called with really shallow stacks...
   canonical->sysno = gst->guest_RAX;
   if (canonical->sysno != __NR_syscall) {
      // stack[0] is return address
      canonical->arg1  = gst->guest_RDI;
      canonical->arg2  = gst->guest_RSI;
      canonical->arg3  = gst->guest_RDX;
      canonical->arg4  = gst->guest_R10;  // not rcx with syscall insn
      canonical->arg5  = gst->guest_R8;
      canonical->arg6  = gst->guest_R9;
      canonical->arg7  = stack[1];
      canonical->arg8  = stack[2];
   } else {
      // GrP fixme hack handle syscall()
      // GrP fixme what about __syscall() ?
      // stack[0] is return address
      canonical->sysno = VG_DARWIN_SYSCALL_CONSTRUCT_UNIX(gst->guest_RDI);
      vg_assert(canonical->sysno != __NR_syscall);
      canonical->arg1  = gst->guest_RSI;
      canonical->arg2  = gst->guest_RDX;
      canonical->arg3  = gst->guest_R10;  // not rcx with syscall insn
      canonical->arg4  = gst->guest_R8;
      canonical->arg5  = gst->guest_R9;
      canonical->arg6  = stack[1];
      canonical->arg7  = stack[2];
      canonical->arg8  = stack[3];
      
      PRINT("SYSCALL[%d,?](%5lld) syscall(#%x, ...); please stand by...\n",
            VG_(getpid)(), /*tid,*/ (Long)0, VG_DARWIN_SYSNO_PRINT(canonical->sysno));
   }

   // no canonical->sysno adjustment needed

#else
#  error "getSyscallArgsFromGuestState: unknown arch"
#endif
}

static 
void putSyscallArgsIntoGuestState ( /*IN*/ SyscallArgs*       canonical,
                                    /*OUT*/VexGuestArchState* gst_vanilla )
{
#if defined(VGP_x86_linux)
   VexGuestX86State* gst = (VexGuestX86State*)gst_vanilla;
   gst->guest_EAX = canonical->sysno;
   gst->guest_EBX = canonical->arg1;
   gst->guest_ECX = canonical->arg2;
   gst->guest_EDX = canonical->arg3;
   gst->guest_ESI = canonical->arg4;
   gst->guest_EDI = canonical->arg5;
   gst->guest_EBP = canonical->arg6;

#elif defined(VGP_amd64_linux)
   VexGuestAMD64State* gst = (VexGuestAMD64State*)gst_vanilla;
   gst->guest_RAX = canonical->sysno;
   gst->guest_RDI = canonical->arg1;
   gst->guest_RSI = canonical->arg2;
   gst->guest_RDX = canonical->arg3;
   gst->guest_R10 = canonical->arg4;
   gst->guest_R8  = canonical->arg5;
   gst->guest_R9  = canonical->arg6;

#elif defined(VGP_ppc32_linux)
   VexGuestPPC32State* gst = (VexGuestPPC32State*)gst_vanilla;
   gst->guest_GPR0 = canonical->sysno;
   gst->guest_GPR3 = canonical->arg1;
   gst->guest_GPR4 = canonical->arg2;
   gst->guest_GPR5 = canonical->arg3;
   gst->guest_GPR6 = canonical->arg4;
   gst->guest_GPR7 = canonical->arg5;
   gst->guest_GPR8 = canonical->arg6;

#elif defined(VGP_ppc64_linux)
   VexGuestPPC64State* gst = (VexGuestPPC64State*)gst_vanilla;
   gst->guest_GPR0 = canonical->sysno;
   gst->guest_GPR3 = canonical->arg1;
   gst->guest_GPR4 = canonical->arg2;
   gst->guest_GPR5 = canonical->arg3;
   gst->guest_GPR6 = canonical->arg4;
   gst->guest_GPR7 = canonical->arg5;
   gst->guest_GPR8 = canonical->arg6;

#elif defined(VGP_ppc32_aix5)
   VexGuestPPC32State* gst = (VexGuestPPC32State*)gst_vanilla;
   gst->guest_GPR2  = canonical->sysno;
   gst->guest_GPR3  = canonical->arg1;
   gst->guest_GPR4  = canonical->arg2;
   gst->guest_GPR5  = canonical->arg3;
   gst->guest_GPR6  = canonical->arg4;
   gst->guest_GPR7  = canonical->arg5;
   gst->guest_GPR8  = canonical->arg6;
   gst->guest_GPR9  = canonical->arg7;
   gst->guest_GPR10 = canonical->arg8;

#elif defined(VGP_ppc64_aix5)
   VexGuestPPC64State* gst = (VexGuestPPC64State*)gst_vanilla;
   gst->guest_GPR2  = canonical->sysno;
   gst->guest_GPR3  = canonical->arg1;
   gst->guest_GPR4  = canonical->arg2;
   gst->guest_GPR5  = canonical->arg3;
   gst->guest_GPR6  = canonical->arg4;
   gst->guest_GPR7  = canonical->arg5;
   gst->guest_GPR8  = canonical->arg6;
   gst->guest_GPR9  = canonical->arg7;
   gst->guest_GPR10 = canonical->arg8;

#elif defined(VGP_x86_darwin)
   VexGuestX86State* gst = (VexGuestX86State*)gst_vanilla;
   UWord *stack = (UWord *)gst->guest_ESP;

   gst->guest_EAX = VG_DARWIN_SYSNO_NUM(canonical->sysno);

   // GrP fixme? gst->guest_TEMP_EFLAG_C = 0;
   // stack[0] is return address
   stack[1] = canonical->arg1;
   stack[2] = canonical->arg2;
   stack[3] = canonical->arg3;
   stack[4] = canonical->arg4;
   stack[5] = canonical->arg5;
   stack[6] = canonical->arg6;
   stack[7] = canonical->arg7;
   stack[8] = canonical->arg8;
   
#elif defined(VGP_amd64_darwin)
   VexGuestAMD64State* gst = (VexGuestAMD64State*)gst_vanilla;
   UWord *stack = (UWord *)gst->guest_RSP;

   gst->guest_RAX = VG_DARWIN_SYSNO_NUM(canonical->sysno);
   // GrP fixme? gst->guest_TEMP_EFLAG_C = 0;

   // stack[0] is return address
   gst->guest_RDI = canonical->arg1;
   gst->guest_RSI = canonical->arg2;
   gst->guest_RDX = canonical->arg3;
   gst->guest_RCX = canonical->arg4;
   gst->guest_R8  = canonical->arg5;
   gst->guest_R9  = canonical->arg6;
   stack[1]       = canonical->arg7;
   stack[2]       = canonical->arg8;

#else
#  error "putSyscallArgsIntoGuestState: unknown arch"
#endif
}

static
void getSyscallStatusFromGuestState ( /*OUT*/SyscallStatus*     canonical,
                                      /*IN*/ VexGuestArchState* gst_vanilla )
{
#  if defined(VGP_x86_linux)
   VexGuestX86State* gst = (VexGuestX86State*)gst_vanilla;
   canonical->sres = VG_(mk_SysRes_x86_linux)( gst->guest_EAX );
   canonical->what = SsComplete;

#  elif defined(VGP_amd64_linux)
   VexGuestAMD64State* gst = (VexGuestAMD64State*)gst_vanilla;
   canonical->sres = VG_(mk_SysRes_amd64_linux)( gst->guest_RAX );
   canonical->what = SsComplete;

#  elif defined(VGP_ppc32_linux)
   VexGuestPPC32State* gst   = (VexGuestPPC32State*)gst_vanilla;
   UInt                cr    = LibVEX_GuestPPC32_get_CR( gst );
   UInt                cr0so = (cr >> 28) & 1;
   canonical->sres = VG_(mk_SysRes_ppc32_linux)( gst->guest_GPR3, cr0so );
   canonical->what = SsComplete;

#  elif defined(VGP_ppc64_linux)
   VexGuestPPC64State* gst   = (VexGuestPPC64State*)gst_vanilla;
   UInt                cr    = LibVEX_GuestPPC64_get_CR( gst );
   UInt                cr0so = (cr >> 28) & 1;
   canonical->sres = VG_(mk_SysRes_ppc64_linux)( gst->guest_GPR3, cr0so );
   canonical->what = SsComplete;

#  elif defined(VGP_ppc32_aix5)
   VexGuestPPC32State* gst = (VexGuestPPC32State*)gst_vanilla;
   canonical->sres = VG_(mk_SysRes_ppc32_aix5)( gst->guest_GPR3, 
                                                gst->guest_GPR4 );
   canonical->what = SsComplete;

#  elif defined(VGP_ppc64_aix5)
   VexGuestPPC64State* gst = (VexGuestPPC64State*)gst_vanilla;
   canonical->sres = VG_(mk_SysRes_ppc64_aix5)( gst->guest_GPR3, 
                                                gst->guest_GPR4 );
   canonical->what = SsComplete;

#  elif defined(VGP_x86_darwin)
   VexGuestX86State* gst = (VexGuestX86State*)gst_vanilla;
   UInt carry = 1 & LibVEX_GuestX86_get_eflags(gst);
   UInt err;
   UWord val;
   UWord val2;

   switch (gst->guest_SC_CLASS) {
   case VG_DARWIN_SYSCALL_CLASS_UX64:
       // int $0x80 = Unix, 64-bit result
       err = carry;
       val = gst->guest_EAX;
       val2 = gst->guest_EDX;
       break;
   case VG_DARWIN_SYSCALL_CLASS_UNIX:
       // syscall = Unix, 32-bit result
       err = carry;
       val = gst->guest_EAX;
       val2 = 0;
       break;
   case VG_DARWIN_SYSCALL_CLASS_MACH:
       // int $0x81 = Mach, 32-bit result
       err = 0;
       val = gst->guest_EAX;
       val2 = 0;
       break;
   case VG_DARWIN_SYSCALL_CLASS_MDEP:
       // int $0x82 = mdep, 32-bit result
       err = 0;
       val = gst->guest_EAX;
       val2 = 0;
       break;
   default: 
       vg_assert(0);
       break;
   }

   if (err) {
       canonical->sres.isError = True;
       canonical->sres.res = 0;
       canonical->sres.res2 = 0;
       canonical->sres.err = val;
   } else {
       canonical->sres.isError = False;
       canonical->sres.res = val;
       canonical->sres.res2 = val2;
       canonical->sres.err = 0;
   }
   canonical->what = SsComplete;

#  elif defined(VGP_amd64_darwin)
   VexGuestAMD64State* gst = (VexGuestAMD64State*)gst_vanilla;
   UInt carry = 1 & LibVEX_GuestAMD64_get_rflags(gst);
   UInt err;
   UWord val;
   UWord val2;

   switch (gst->guest_SC_CLASS) {
   case VG_DARWIN_SYSCALL_CLASS_UX64:
       // int $0x80 = Unix, 128-bit result
       err = carry;
       val = gst->guest_RAX;
       val2 = gst->guest_RDX;
       break;
   case VG_DARWIN_SYSCALL_CLASS_UNIX:
       // syscall = Unix, 64-bit result
       err = carry;
       val = gst->guest_RAX;
       val2 = 0;
       break;
   case VG_DARWIN_SYSCALL_CLASS_MACH:
       // int $0x81 = Mach, 64-bit result
       err = 0;
       val = gst->guest_RAX;
       val2 = 0;
       break;
   case VG_DARWIN_SYSCALL_CLASS_MDEP:
       // int $0x82 = mdep, 64-bit result
       err = 0;
       val = gst->guest_RAX;
       val2 = 0;
       break;
   default: 
       vg_assert(0);
       break;
   }

   if (err) {
       canonical->sres.isError = True;
       canonical->sres.res = 0;
       canonical->sres.res2 = 0;
       canonical->sres.err = val;
   } else {
       canonical->sres.isError = False;
       canonical->sres.res = val;
       canonical->sres.res2 = val2;
       canonical->sres.err = 0;
   }
   canonical->what = SsComplete;

#  else
#    error "getSyscallStatusFromGuestState: unknown arch"
#  endif
}

static 
void putSyscallStatusIntoGuestState ( /*IN*/ ThreadId tid, 
                                      /*IN*/ SyscallStatus*     canonical,
                                      /*OUT*/VexGuestArchState* gst_vanilla )
{
#  if defined(VGP_x86_linux)
   VexGuestX86State* gst = (VexGuestX86State*)gst_vanilla;
   vg_assert(canonical->what == SsComplete);
   if (canonical->sres.isError) {
      /* This isn't exactly right, in that really a Failure with res
         not in the range 1 .. 4095 is unrepresentable in the
         Linux-x86 scheme.  Oh well. */
      gst->guest_EAX = - (Int)canonical->sres.err;
   } else {
      gst->guest_EAX = canonical->sres.res;
   }
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
             OFFSET_x86_EAX, sizeof(UWord) );

#  elif defined(VGP_amd64_linux)
   VexGuestAMD64State* gst = (VexGuestAMD64State*)gst_vanilla;
   vg_assert(canonical->what == SsComplete);
   if (canonical->sres.isError) {
      /* This isn't exactly right, in that really a Failure with res
         not in the range 1 .. 4095 is unrepresentable in the
         Linux-x86 scheme.  Oh well. */
      gst->guest_RAX = - (Long)canonical->sres.err;
   } else {
      gst->guest_RAX = canonical->sres.res;
   }
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
             OFFSET_amd64_RAX, sizeof(UWord) );

#  elif defined(VGP_ppc32_linux)
   VexGuestPPC32State* gst = (VexGuestPPC32State*)gst_vanilla;
   UInt old_cr = LibVEX_GuestPPC32_get_CR(gst);
   vg_assert(canonical->what == SsComplete);
   if (canonical->sres.isError) {
      /* set CR0.SO */
      LibVEX_GuestPPC32_put_CR( old_cr | (1<<28), gst );
      gst->guest_GPR3 = canonical->sres.err;
   } else {
      /* clear CR0.SO */
      LibVEX_GuestPPC32_put_CR( old_cr & ~(1<<28), gst );
      gst->guest_GPR3 = canonical->sres.res;
   }
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
             OFFSET_ppc32_GPR3, sizeof(UWord) );
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
             OFFSET_ppc32_CR0_0, sizeof(UChar) );

#  elif defined(VGP_ppc64_linux)
   VexGuestPPC64State* gst = (VexGuestPPC64State*)gst_vanilla;
   UInt old_cr = LibVEX_GuestPPC64_get_CR(gst);
   vg_assert(canonical->what == SsComplete);
   if (canonical->sres.isError) {
      /* set CR0.SO */
      LibVEX_GuestPPC64_put_CR( old_cr | (1<<28), gst );
      gst->guest_GPR3 = canonical->sres.err;
   } else {
      /* clear CR0.SO */
      LibVEX_GuestPPC64_put_CR( old_cr & ~(1<<28), gst );
      gst->guest_GPR3 = canonical->sres.res;
   }
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
             OFFSET_ppc64_GPR3, sizeof(UWord) );
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
             OFFSET_ppc64_CR0_0, sizeof(UChar) );

#  elif defined(VGP_ppc32_aix5)
   VexGuestPPC32State* gst = (VexGuestPPC32State*)gst_vanilla;
   vg_assert(canonical->what == SsComplete);
   gst->guest_GPR3 = canonical->sres.res;
   gst->guest_GPR4 = canonical->sres.err;
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
             OFFSET_ppc32_GPR3, sizeof(UWord) );
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
             OFFSET_ppc32_GPR3, sizeof(UWord) );

#  elif defined(VGP_ppc64_aix5)
   VexGuestPPC64State* gst = (VexGuestPPC64State*)gst_vanilla;
   vg_assert(canonical->what == SsComplete);
   gst->guest_GPR3 = canonical->sres.res;
   gst->guest_GPR4 = canonical->sres.err;
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
             OFFSET_ppc64_GPR3, sizeof(UWord) );
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
             OFFSET_ppc64_GPR4, sizeof(UWord) );

#elif defined(VGP_x86_darwin)
   VexGuestX86State* gst = (VexGuestX86State*)gst_vanilla;
   UWord val = 
       canonical->sres.isError ? canonical->sres.err : canonical->sres.res;
   vg_assert(canonical->what == SsComplete);

   switch (gst->guest_SC_CLASS) {
   case VG_DARWIN_SYSCALL_CLASS_UX64:
       // int $0x80 = Unix, 64-bit result
       if (!canonical->sres.isError) gst->guest_EDX = canonical->sres.res2;
       VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
                 OFFSET_x86_EDX, sizeof(UWord) );
       // FALL-THROUGH to VG_DARWIN_SYSCALL_CLASS_UNIX
   case VG_DARWIN_SYSCALL_CLASS_UNIX:
       // syscall = Unix, 32-bit result
       gst->guest_EAX = val;
       LibVEX_GuestX86_put_eflag_c(canonical->sres.isError, gst);
       VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
                 OFFSET_x86_EAX, sizeof(UWord) );
       // fixme sets defined for entire eflags, not just bit c
       // DDD: this breaks exp-ptrcheck.
       VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
                 offsetof(VexGuestX86State, guest_CC_DEP1), sizeof(UInt) );
       break;
   case VG_DARWIN_SYSCALL_CLASS_MACH:
       // int $0x81 = Mach, 32-bit result
       gst->guest_EAX = val;
       VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
                 OFFSET_x86_EAX, sizeof(UWord) );
       break;
   case VG_DARWIN_SYSCALL_CLASS_MDEP:
       // int $0x82 = mdep, 32-bit result
       gst->guest_EAX = val;
       VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
                 OFFSET_x86_EAX, sizeof(UWord) );
       break;
   default: 
       vg_assert(0);
       break;
   }
   
#elif defined(VGP_amd64_darwin)
   VexGuestAMD64State* gst = (VexGuestAMD64State*)gst_vanilla;
   UWord val = 
       canonical->sres.isError ? canonical->sres.err : canonical->sres.res;
   vg_assert(canonical->what == SsComplete);

   switch (gst->guest_SC_CLASS) {
   case VG_DARWIN_SYSCALL_CLASS_UNIX:
       // syscall = Unix, 32-bit result
       if (!canonical->sres.isError) gst->guest_RDX = canonical->sres.res2;
       VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
                 OFFSET_amd64_RDX, sizeof(UWord) );
       gst->guest_RAX = val;
       LibVEX_GuestAMD64_put_rflag_c(canonical->sres.isError, gst);
       VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
                 OFFSET_amd64_RAX, sizeof(UWord) );
       // fixme sets defined for entire rflags, not just bit c
       VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
                 offsetof(VexGuestAMD64State, guest_CC_DEP1), sizeof(ULong) );
       break;
   case VG_DARWIN_SYSCALL_CLASS_MACH:
       // int $0x81 = Mach, 32-bit result
       gst->guest_RAX = val;
       VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
                 OFFSET_amd64_RAX, sizeof(UWord) );
       break;
   case VG_DARWIN_SYSCALL_CLASS_MDEP:
       // int $0x82 = mdep, 32-bit result
       gst->guest_RAX = val;
       VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, 
                 OFFSET_amd64_RAX, sizeof(UWord) );
       break;
   default: 
       vg_assert(0);
       break;
   }
   
#  else
#    error "putSyscallStatusIntoGuestState: unknown arch"
#  endif
}


/* Tell me the offsets in the guest state of the syscall params, so
   that the scalar argument checkers don't have to have this info
   hardwired. */

static
void getSyscallArgLayout ( /*OUT*/SyscallArgLayout* layout )
{
#if defined(VGP_x86_linux)
   layout->o_sysno  = OFFSET_x86_EAX;
   layout->o_arg1   = OFFSET_x86_EBX;
   layout->o_arg2   = OFFSET_x86_ECX;
   layout->o_arg3   = OFFSET_x86_EDX;
   layout->o_arg4   = OFFSET_x86_ESI;
   layout->o_arg5   = OFFSET_x86_EDI;
   layout->o_arg6   = OFFSET_x86_EBP;
   layout->dummy_arg7 = -1; /* impossible value */
   layout->dummy_arg8 = -1; /* impossible value */
   layout->o_retval = OFFSET_x86_EAX;

#elif defined(VGP_amd64_linux)
   layout->o_sysno  = OFFSET_amd64_RAX;
   layout->o_arg1   = OFFSET_amd64_RDI;
   layout->o_arg2   = OFFSET_amd64_RSI;
   layout->o_arg3   = OFFSET_amd64_RDX;
   layout->o_arg4   = OFFSET_amd64_R10;
   layout->o_arg5   = OFFSET_amd64_R8;
   layout->o_arg6   = OFFSET_amd64_R9;
   layout->dummy_arg7 = -1; /* impossible value */
   layout->dummy_arg8 = -1; /* impossible value */
   layout->o_retval = OFFSET_amd64_RAX;

#elif defined(VGP_ppc32_linux)
   layout->o_sysno  = OFFSET_ppc32_GPR0;
   layout->o_arg1   = OFFSET_ppc32_GPR3;
   layout->o_arg2   = OFFSET_ppc32_GPR4;
   layout->o_arg3   = OFFSET_ppc32_GPR5;
   layout->o_arg4   = OFFSET_ppc32_GPR6;
   layout->o_arg5   = OFFSET_ppc32_GPR7;
   layout->o_arg6   = OFFSET_ppc32_GPR8;
   layout->o_arg7   = -1; /* impossible value */
   layout->o_arg8   = -1; /* impossible value */
   layout->o_retval = OFFSET_ppc32_GPR3;

#elif defined(VGP_ppc64_linux)
   layout->o_sysno  = OFFSET_ppc64_GPR0;
   layout->o_arg1   = OFFSET_ppc64_GPR3;
   layout->o_arg2   = OFFSET_ppc64_GPR4;
   layout->o_arg3   = OFFSET_ppc64_GPR5;
   layout->o_arg4   = OFFSET_ppc64_GPR6;
   layout->o_arg5   = OFFSET_ppc64_GPR7;
   layout->o_arg6   = OFFSET_ppc64_GPR8;
   layout->o_arg7   = -1; /* impossible value */
   layout->o_arg8   = -1; /* impossible value */
   layout->o_retval = OFFSET_ppc64_GPR3;

#elif defined(VGP_ppc32_aix5)
   layout->o_sysno  = OFFSET_ppc32_GPR2;
   layout->o_arg1   = OFFSET_ppc32_GPR3;
   layout->o_arg2   = OFFSET_ppc32_GPR4;
   layout->o_arg3   = OFFSET_ppc32_GPR5;
   layout->o_arg4   = OFFSET_ppc32_GPR6;
   layout->o_arg5   = OFFSET_ppc32_GPR7;
   layout->o_arg6   = OFFSET_ppc32_GPR8;
   layout->o_arg7   = OFFSET_ppc32_GPR9;
   layout->o_arg8   = OFFSET_ppc32_GPR10;
   layout->o_retval = OFFSET_ppc32_GPR3;

#elif defined(VGP_ppc64_aix5)
   layout->o_sysno  = OFFSET_ppc64_GPR2;
   layout->o_arg1   = OFFSET_ppc64_GPR3;
   layout->o_arg2   = OFFSET_ppc64_GPR4;
   layout->o_arg3   = OFFSET_ppc64_GPR5;
   layout->o_arg4   = OFFSET_ppc64_GPR6;
   layout->o_arg5   = OFFSET_ppc64_GPR7;
   layout->o_arg6   = OFFSET_ppc64_GPR8;
   layout->o_arg7   = OFFSET_ppc64_GPR9;
   layout->o_arg8   = OFFSET_ppc64_GPR10;
   layout->o_retval = OFFSET_ppc64_GPR3;

#elif defined(VGP_x86_darwin)
   layout->o_sysno  = OFFSET_x86_EAX;
   layout->o_retval_lo = OFFSET_x86_EAX;
   layout->o_retval_hi = OFFSET_x86_EDX;
   // syscall parameters are on stack in C convention
   layout->s_arg1   = sizeof(UWord) * 1;
   layout->s_arg2   = sizeof(UWord) * 2;
   layout->s_arg3   = sizeof(UWord) * 3;
   layout->s_arg4   = sizeof(UWord) * 4;
   layout->s_arg5   = sizeof(UWord) * 5;
   layout->s_arg6   = sizeof(UWord) * 6;
   layout->s_arg7   = sizeof(UWord) * 7;
   layout->s_arg8   = sizeof(UWord) * 8;
   
#elif defined(VGP_amd64_darwin)
   layout->o_sysno  = OFFSET_amd64_RAX;
   layout->o_arg1   = OFFSET_amd64_RDI;
   layout->o_arg2   = OFFSET_amd64_RSI;
   layout->o_arg3   = OFFSET_amd64_RDX;
   layout->o_arg4   = OFFSET_amd64_RCX;
   layout->o_arg5   = OFFSET_amd64_R8;
   layout->o_arg6   = OFFSET_amd64_R9;
   layout->s_arg7   = sizeof(UWord) * 1;
   layout->s_arg8   = sizeof(UWord) * 2;
   layout->o_retval_lo = OFFSET_amd64_RAX;
   layout->o_retval_hi = OFFSET_amd64_RDX;

#else
#  error "getSyscallLayout: unknown arch"
#endif
}


/* ---------------------------------------------------------------------
   The main driver logic
   ------------------------------------------------------------------ */

/* Finding the handlers for a given syscall, or faking up one
   when no handler is found. */

static 
void bad_before ( ThreadId              tid,
                  SyscallArgLayout*     layout,
                  /*MOD*/SyscallArgs*   args,
                  /*OUT*/SyscallStatus* status,
                  /*OUT*/UWord*         flags )
{
   VG_(message)
      (Vg_DebugMsg,"WARNING: unhandled syscall: %lld", (Long)args->sysno);
   // DDD: make this generic with a common function.
#  if defined(VGO_linux)
   // nothing
#  elif defined(VGO_aix5)
   VG_(message)
      (Vg_DebugMsg,"           name of syscall: \"%s\"",
                    VG_(aix5_sysno_to_sysname)(args->sysno));
#  elif defined(VGO_darwin)
   VG_(message)
      (Vg_DebugMsg,"           a.k.a.: %lld",
                    (Long)VG_DARWIN_SYSNO_PRINT(args->sysno));
#  else
#     error unknown OS
#  endif
   if (VG_(clo_verbosity) > 1) {
      VG_(get_and_pp_StackTrace)(tid, VG_(clo_backtrace_size));
   }
   VG_(message)
      (Vg_DebugMsg,"You may be able to write your own handler.");
   VG_(message)
      (Vg_DebugMsg,"Read the file README_MISSING_SYSCALL_OR_IOCTL.");
   VG_(message)
      (Vg_DebugMsg,"Nevertheless we consider this a bug.  Please report");
   VG_(message)
      (Vg_DebugMsg,"it at http://valgrind.org/support/bug_reports.html.");

   SET_STATUS_Failure(VKI_ENOSYS);
}

static SyscallTableEntry bad_sys =
   { bad_before, NULL };

static const SyscallTableEntry* get_syscall_entry ( Int syscallno, 
                                                    ThreadState *tst )
{
   const SyscallTableEntry* sys = NULL;

#  if defined(VGO_linux)
   if (syscallno < ML_(syscall_table_size) &&
       ML_(syscall_table)[syscallno].before != NULL)
      sys = &ML_(syscall_table)[syscallno];

#  elif defined(VGP_ppc32_aix5)
   sys = ML_(get_ppc32_aix5_syscall_entry) ( syscallno );

#  elif defined(VGP_ppc64_aix5)
   sys = ML_(get_ppc64_aix5_syscall_entry) ( syscallno );

#  elif defined(VGO_darwin)
   Int idx = VG_DARWIN_SYSNO_INDEX(syscallno);

   switch (VG_DARWIN_SYSNO_CLASS(syscallno)) {
   case VG_DARWIN_SYSCALL_CLASS_UX64:
   case VG_DARWIN_SYSCALL_CLASS_UNIX:
      if (idx >= 0 && idx < ML_(syscall_table_size) &&
          ML_(syscall_table)[idx].before != NULL)
         sys = &ML_(syscall_table)[idx];
         break;
   case VG_DARWIN_SYSCALL_CLASS_MACH:
      if (idx >= 0 && idx < ML_(mach_trap_table_size) &&
          ML_(mach_trap_table)[idx].before != NULL)
         sys = &ML_(mach_trap_table)[idx];
         break;
   case VG_DARWIN_SYSCALL_CLASS_MDEP:
      if (idx >= 0 && idx < ML_(mdep_trap_table_size) &&
          ML_(mdep_trap_table)[idx].before != NULL)
         sys = &ML_(mdep_trap_table)[idx];
         break;
   default: 
      vg_assert(0);
      break;
   }

#  else
#    error Unknown OS
#  endif

   return sys == NULL  ? &bad_sys  : sys;
}


/* Add and remove signals from mask so that we end up telling the
   kernel the state we actually want rather than what the client
   wants. */
static void sanitize_client_sigmask(vki_sigset_t *mask)
{
   VG_(sigdelset)(mask, VKI_SIGKILL);
   VG_(sigdelset)(mask, VKI_SIGSTOP);
   VG_(sigdelset)(mask, VG_SIGVGKILL); /* never block */
}

typedef
   struct {
      SyscallArgs   orig_args;
      SyscallArgs   args;
      SyscallStatus status;
      UWord         flags;
   }
   SyscallInfo;

SyscallInfo syscallInfo[VG_N_THREADS];


/* The scheduler needs to be able to zero out these records after a
   fork, hence this is exported from m_syswrap. */
void VG_(clear_syscallInfo) ( Int tid )
{
   vg_assert(tid >= 0 && tid < VG_N_THREADS);
   VG_(memset)( & syscallInfo[tid], 0, sizeof( syscallInfo[tid] ));
   syscallInfo[tid].status.what = SsIdle;
}

static void ensure_initialised ( void )
{
   Int i;
   static Bool init_done = False;
   if (init_done) 
      return;
   init_done = True;
   for (i = 0; i < VG_N_THREADS; i++) {
      VG_(clear_syscallInfo)( i );
   }
}

/* --- This is the main function of this file. --- */

void VG_(client_syscall) ( ThreadId tid, UInt trc )
{
   Word                    sysno;
   ThreadState*             tst;
   const SyscallTableEntry* ent;
   SyscallArgLayout         layout;
   SyscallInfo*             sci;

   ensure_initialised();

   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(tid >= 1 && tid < VG_N_THREADS);
   vg_assert(VG_(is_running_thread)(tid));

   tst = VG_(get_ThreadState)(tid);

   /* BEGIN ensure root thread's stack is suitably mapped */
   /* In some rare circumstances, we may do the syscall without the
      bottom page of the stack being mapped, because the stack pointer
      was moved down just a few instructions before the syscall
      instruction, and there have been no memory references since
      then, that would cause a call to VG_(extend_stack) to have
      happened.

      In native execution that's OK: the kernel automagically extends
      the stack's mapped area down to cover the stack pointer (or sp -
      redzone, really).  In simulated normal execution that's OK too,
      since any signals we get from accessing below the mapped area of
      the (guest's) stack lead us to VG_(extend_stack), where we
      simulate the kernel's stack extension logic.  But that leaves
      the problem of entering a syscall with the SP unmapped.  Because
      the kernel doesn't know that the segment immediately above SP is
      supposed to be a grow-down segment, it causes the syscall to
      fail, and thereby causes a divergence between native behaviour
      (syscall succeeds) and simulated behaviour (syscall fails).

      This is quite a rare failure mode.  It has only been seen
      affecting calls to sys_readlink on amd64-linux, and even then it
      requires a certain code sequence around the syscall to trigger
      it.  Here is one:

      extern int my_readlink ( const char* path );
      asm(
      ".text\n"
      ".globl my_readlink\n"
      "my_readlink:\n"
      "\tsubq    $0x1008,%rsp\n"
      "\tmovq    %rdi,%rdi\n"              // path is in rdi
      "\tmovq    %rsp,%rsi\n"              // &buf[0] -> rsi
      "\tmovl    $0x1000,%edx\n"           // sizeof(buf) in rdx
      "\tmovl    $"__NR_READLINK",%eax\n"  // syscall number
      "\tsyscall\n"
      "\taddq    $0x1008,%rsp\n"
      "\tret\n"
      ".previous\n"
      );

      For more details, see bug #156404
      (https://bugs.kde.org/show_bug.cgi?id=156404).

      The fix is actually very simple.  We simply need to call
      VG_(extend_stack) for this thread, handing it the lowest
      possible valid address for stack (sp - redzone), to ensure the
      pages all the way down to that address, are mapped.  Because
      this is a potentially expensive and frequent operation, we
      filter in two ways:

      First, only the main thread (tid=1) has a growdown stack.  So
      ignore all others.  It is conceivable, although highly unlikely,
      that the main thread exits, and later another thread is
      allocated tid=1, but that's harmless, I believe;
      VG_(extend_stack) will do nothing when applied to a non-root
      thread.

      Secondly, first call VG_(am_find_nsegment) directly, to see if
      the page holding (sp - redzone) is mapped correctly.  If so, do
      nothing.  This is almost always the case.  VG_(extend_stack)
      calls VG_(am_find_nsegment) twice, so this optimisation -- and
      that's all it is -- more or less halves the number of calls to
      VG_(am_find_nsegment) required.

      TODO: the test "seg->kind == SkAnonC" is really inadequate,
      because although it tests whether the segment is mapped
      _somehow_, it doesn't check that it has the right permissions
      (r,w, maybe x) ?  We could test that here, but it will also be
      necessary to fix the corresponding test in VG_(extend_stack).

      All this guff is of course Linux-specific.  Hence the ifdef.
   */
#  if defined(VGO_linux)
   if (tid == 1/*ROOT THREAD*/) {
      Addr     stackMin   = VG_(get_SP)(tid) - VG_STACK_REDZONE_SZB;
      NSegment const* seg = VG_(am_find_nsegment)(stackMin);
      if (seg && seg->kind == SkAnonC) {
         /* stackMin is already mapped.  Nothing to do. */
      } else {
         (void)VG_(extend_stack)( stackMin,
                                  tst->client_stack_szB );
      }
   }
#  endif
   /* END ensure root thread's stack is suitably mapped */

   /* First off, get the syscall args and number.  This is a
      platform-dependent action. */

   sci = & syscallInfo[tid];
   vg_assert(sci->status.what == SsIdle);

   getSyscallArgsFromGuestState( &sci->orig_args, &tst->arch.vex, trc );

   /* Copy .orig_args to .args.  The pre-handler may modify .args, but
      we want to keep the originals too, just in case. */
   sci->args = sci->orig_args;

   /* Save the syscall number in the thread state in case the syscall 
      is interrupted by a signal. */
   sysno = sci->orig_args.sysno;

#if defined(VGO_darwin)
   // Record syscall class.
   tst->arch.vex.guest_SC_CLASS = VG_DARWIN_SYSNO_CLASS(sysno);
#endif

   /* The default what-to-do-next thing is hand the syscall to the
      kernel, so we pre-set that here.  Set .sres to something
      harmless looking (is irrelevant because .what is not
      SsComplete.) */
   sci->status.what = SsHandToKernel;
   sci->status.sres = VG_(mk_SysRes_Error)(0);
   sci->flags       = 0;

   /* Fetch the syscall's handlers.  If no handlers exist for this
      syscall, we are given dummy handlers which force an immediate
      return with ENOSYS. */
   ent = get_syscall_entry(sysno, tst);

   /* Fetch the layout information, which tells us where in the guest
      state the syscall args reside.  This is a platform-dependent
      action.  This info is needed so that the scalar syscall argument
      checks (PRE_REG_READ calls) know which bits of the guest state
      they need to inspect. */
   getSyscallArgLayout( &layout );

   /* Make sure the tmp signal mask matches the real signal mask;
      sigsuspend may change this. */
   vg_assert(VG_(iseqsigset)(&tst->sig_mask, &tst->tmp_sig_mask));

   /* Right, we're finally ready to Party.  Call the pre-handler and
      see what we get back.  At this point: 

        sci->status.what  is Unset (we don't know yet).
        sci->orig_args    contains the original args.
        sci->args         is the same as sci->orig_args.
        sci->flags        is zero.
   */

   {
      PRINT("SYSCALL[%d,%d](%5lld) ", VG_(getpid)(), tid, (Long)
      // DDD: make this generic
      #if defined(VGO_linux) || defined(VGO_aix5)
         sysno
      #elif defined(VGO_darwin)
         VG_DARWIN_SYSNO_PRINT(sysno)
      #else
      #  error Unknown OS
      #endif
      );
   // VG_(check_segments)("### before syscall");
   }

   /* Do any pre-syscall actions */
   if (VG_(needs).syscall_wrapper) {
      VG_TDICT_CALL(tool_pre_syscall, tid, sysno);
   }

   vg_assert(ent);
   vg_assert(ent->before);
   (ent->before)( tid,
                  &layout, 
                  &sci->args, &sci->status, &sci->flags );
   
   /* The pre-handler may have modified:
         sci->args
         sci->status
         sci->flags
      All else remains unchanged. 
      Although the args may be modified, pre handlers are not allowed
      to change the syscall number.
   */
   /* Now we proceed according to what the pre-handler decided. */
   vg_assert(sci->status.what == SsHandToKernel
             || sci->status.what == SsComplete);
   vg_assert(sci->args.sysno == sci->orig_args.sysno);

   if (sci->status.what == SsComplete && !sci->status.sres.isError) {
      /* The pre-handler completed the syscall itself, declaring
         success. */
      if (sci->flags & SfNoWriteResult) {
         PRINT(" --> [pre-success] NoWriteResult");
      } else {
         PRINT(" --> [pre-success] Success(0x%llx)",
               (ULong)sci->status.sres.res );
      }                                      
      /* In this case the allowable flags are to ask for a signal-poll
         and/or a yield after the call.  Changing the args isn't
         allowed. */
      vg_assert(0 == (sci->flags 
                      & ~(SfPollAfter | SfYieldAfter | SfNoWriteResult)));
      vg_assert(eq_SyscallArgs(&sci->args, &sci->orig_args));
   }

   else
   if (sci->status.what == SsComplete && sci->status.sres.isError) {
      /* The pre-handler decided to fail syscall itself. */
      PRINT(" --> [pre-fail] Failure(0x%llx)", (ULong)sci->status.sres.err );
      /* In this case, the pre-handler is also allowed to ask for the
         post-handler to be run anyway.  Changing the args is not
         allowed. */
      vg_assert(0 == (sci->flags & ~(SfMayBlock | SfPostOnFail | SfPollAfter)));
      vg_assert(eq_SyscallArgs(&sci->args, &sci->orig_args));
   }

   else
   if (sci->status.what != SsHandToKernel) {
      /* huh?! */
      vg_assert(0);
   }

   else /* (sci->status.what == HandToKernel) */ {
      /* Ok, this is the usual case -- and the complicated one.  There
         are two subcases: sync and async.  async is the general case
         and is to be used when there is any possibility that the
         syscall might block [a fact that the pre-handler must tell us
         via the sci->flags field.]  Because the tidying-away /
         context-switch overhead of the async case could be large, if
         we are sure that the syscall will not block, we fast-track it
         by doing it directly in this thread, which is a lot
         simpler. */

      /* Check that the given flags are allowable: MayBlock, PollAfter
         and PostOnFail are ok. */
      vg_assert(0 == (sci->flags & ~(SfMayBlock | SfPostOnFail | SfPollAfter)));

      if (sci->flags & SfMayBlock) {

         /* Syscall may block, so run it asynchronously */
         vki_sigset_t mask;

         PRINT(" --> [async] ... \n");

         mask = tst->sig_mask;
         sanitize_client_sigmask(&mask);

         /* Gack.  More impedance matching.  Copy the possibly
            modified syscall args back into the guest state. */
         vg_assert(eq_SyscallArgs(&sci->args, &sci->orig_args));
         putSyscallArgsIntoGuestState( &sci->args, &tst->arch.vex );

         /* Drop the lock */
         VG_(release_BigLock)(tid, VgTs_WaitSys, "VG_(client_syscall)[async]");

         /* Do the call, which operates directly on the guest state,
            not on our abstracted copies of the args/result. */
         do_syscall_for_client(sysno, tst, &mask);

         /* do_syscall_for_client may not return if the syscall was
            interrupted by a signal.  In that case, flow of control is
            first to m_signals.async_sighandler, which calls
            VG_(fixup_guest_state_after_syscall_interrupted), which
            fixes up the guest state, and possibly calls
            VG_(post_syscall).  Once that's done, control drops back
            to the scheduler.  */
         /* Darwin: do_syscall_for_client may not return if the 
            syscall was workq_ops(WQOPS_THREAD_RETURN) and the kernel 
            responded by starting the thread at wqthread_hijack(reuse=1)
            (to run another workqueue item). In that case, wqthread_hijack 
            calls ML_(wqthread_continue), which is similar to 
            VG_(fixup_guest_state_after_syscall_interrupted). */

         /* Reacquire the lock */
         VG_(acquire_BigLock)(tid, "VG_(client_syscall)[async]");

         /* Even more impedance matching.  Extract the syscall status
            from the guest state. */
         getSyscallStatusFromGuestState( &sci->status, &tst->arch.vex );
         vg_assert(sci->status.what == SsComplete);

         PRINT("SYSCALL[%d,%d](%5ld) ... [async] --> %s(0x%llx)",
               VG_(getpid)(), tid, 
               // DDD: make this generic
               #if defined(VGO_linux) || defined(VGO_aix5)
                  sysno
               #elif defined(VGO_darwin)
                  VG_DARWIN_SYSNO_PRINT(sysno)
               #else
               #  error Unknown OS
               #endif
               ,
               sci->status.sres.isError ? "Failure" : "Success",
               sci->status.sres.isError ? (ULong)sci->status.sres.err
                                        : (ULong)sci->status.sres.res );

      } else {

         /* run the syscall directly */
         /* The pre-handler may have modified the syscall args, but
            since we're passing values in ->args directly to the
            kernel, there's no point in flushing them back to the
            guest state.  Indeed doing so could be construed as
            incorrect. */
         SysRes sres 
            = VG_(do_syscall)(sysno, sci->args.arg1, sci->args.arg2, 
                                     sci->args.arg3, sci->args.arg4, 
                                     sci->args.arg5, sci->args.arg6,
                                     sci->args.arg7, sci->args.arg8 );
         sci->status = convert_SysRes_to_SyscallStatus(sres);

         PRINT("[sync] --> %s(0x%llx)",
               sci->status.sres.isError ? "Failure" : "Success",
               sci->status.sres.isError ? (ULong)sci->status.sres.err
                                        : (ULong)sci->status.sres.res );
      }
   }

   vg_assert(sci->status.what == SsComplete);

   vg_assert(VG_(is_running_thread)(tid));

   /* Dump the syscall result back in the guest state.  This is
      a platform-specific action. */
   if (!(sci->flags & SfNoWriteResult))
      putSyscallStatusIntoGuestState( tid, &sci->status, &tst->arch.vex );

   /* Situation now:
      - the guest state is now correctly modified following the syscall
      - modified args, original args and syscall status are still
        available in the syscallInfo[] entry for this syscall.

      Now go on to do the post-syscall actions (read on down ..)
   */
   PRINT(" ");
   VG_(post_syscall)(tid);
   PRINT("\n");
}


/* Perform post syscall actions.  The expected state on entry is
   precisely as at the end of VG_(client_syscall), that is:

   - guest state up to date following the syscall
   - modified args, original args and syscall status are still
     available in the syscallInfo[] entry for this syscall.
   - syscall status matches what's in the guest state.

   There are two ways to get here: the normal way -- being called by
   VG_(client_syscall), and the unusual way, from
   VG_(fixup_guest_state_after_syscall_interrupted).
   Darwin: there's a third way, ML_(wqthread_continue). 
*/
void VG_(post_syscall) (ThreadId tid)
{
   //SyscallArgLayout         layout;     DDD (see below)
   SyscallInfo*             sci;
   const SyscallTableEntry* ent;
   SyscallStatus            test_status;
   ThreadState*             tst;
   Word sysno;

   /* Preliminaries */
   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(tid >= 1 && tid < VG_N_THREADS);
   vg_assert(VG_(is_running_thread)(tid));

   tst = VG_(get_ThreadState)(tid);
   sci = & syscallInfo[tid];

   /* m_signals.sigvgkill_handler might call here even when not in
      a syscall. */
   if (sci->status.what == SsIdle || sci->status.what == SsHandToKernel) {
      sci->status.what = SsIdle;
      return;
   }

   /* Validate current syscallInfo entry.  In particular we require
      that the current .status matches what's actually in the guest
      state.  At least in the normal case where we have actually
      previously written the result into the guest state. */
   vg_assert(sci->status.what == SsComplete);

   getSyscallStatusFromGuestState( &test_status, &tst->arch.vex );
   if (!(sci->flags & SfNoWriteResult))
      vg_assert(eq_SyscallStatus( &sci->status, &test_status ));
   /* Ok, looks sane */

   /* Get the system call number.  Because the pre-handler isn't
      allowed to mess with it, it should be the same for both the
      original and potentially-modified args. */
   vg_assert(sci->args.sysno == sci->orig_args.sysno);
   sysno = sci->args.sysno;
   ent = get_syscall_entry(sysno, tst);

   // DDD: the trunk has the following code...
#if 0
   /* We need the arg layout .. sigh */
   getSyscallArgLayout( &layout );

   /* Tell the tool that the assignment has occurred, so it can update
      shadow regs as necessary. */
   VG_TRACK( post_reg_write, Vg_CoreSysCall, tid, layout.o_retval,
                                                  sizeof(UWord) );
#endif

   /* pre: status == Complete (asserted above) */
   /* Consider either success or failure.  Now run the post handler if:
      - it exists, and
      - Success or (Failure and PostOnFail is set)
   */
   if (ent->after
       && ((!sci->status.sres.isError)
           || (sci->status.sres.isError
               && (sci->flags & SfPostOnFail) ))) {

      (ent->after)( tid, &sci->args, &sci->status );
   }

   /* Because the post handler might have changed the status (eg, the
      post-handler for sys_open can change the result from success to
      failure if the kernel supplied a fd that it doesn't like), once
      again dump the syscall result back in the guest state.*/
   if (!(sci->flags & SfNoWriteResult))
      putSyscallStatusIntoGuestState( tid, &sci->status, &tst->arch.vex );

   /* Do any post-syscall actions required by the tool. */
   if (VG_(needs).syscall_wrapper)
      VG_TDICT_CALL(tool_post_syscall, tid, sysno, sci->status.sres);

   /* The syscall is done. */
   vg_assert(sci->status.what == SsComplete);
   sci->status.what = SsIdle;

   /* The pre/post wrappers may have concluded that pending signals
      might have been created, and will have set SfPollAfter to
      request a poll for them once the syscall is done. */
#if defined(VGO_darwin)
   // DDD: # warning GrP fixme signals
#else
   if (sci->flags & SfPollAfter)
      VG_(poll_signals)(tid);
#endif

   /* Similarly, the wrappers might have asked for a yield
      afterwards. */
   if (sci->flags & SfYieldAfter)
      VG_(vg_yield)();
}


/* ---------------------------------------------------------------------
   Dealing with syscalls which get interrupted by a signal:
   VG_(fixup_guest_state_after_syscall_interrupted)
   ------------------------------------------------------------------ */

/* Syscalls done on behalf of the client are finally handed off to the
   kernel in VG_(client_syscall) above, either by calling
   do_syscall_for_client (the async case), or by calling
   VG_(do_syscall6) (the sync case).

   If the syscall is not interrupted by a signal (it may block and
   later unblock, but that's irrelevant here) then those functions
   eventually return and so control is passed to VG_(post_syscall).
   NB: not sure if the sync case can actually get interrupted, as it
   operates with all signals masked.

   However, the syscall may get interrupted by an async-signal.  In
   that case do_syscall_for_client/VG_(do_syscall6) do not
   return.  Instead we wind up in m_signals.async_sighandler.  We need
   to fix up the guest state to make it look like the syscall was
   interrupted for guest.  So async_sighandler calls here, and this
   does the fixup.  Note that from here we wind up calling
   VG_(post_syscall) too.
*/


/* These are addresses within ML_(do_syscall_for_client_WRK).  See
   syscall-$PLAT.S for details. 
*/
extern const Addr ML_(blksys_setup);
extern const Addr ML_(blksys_restart);
extern const Addr ML_(blksys_complete);
extern const Addr ML_(blksys_committed);
extern const Addr ML_(blksys_finished);


/* Back up guest state to restart a system call. */

void ML_(fixup_guest_state_to_restart_syscall) ( ThreadArchState* arch )
{
#if defined(VGP_x86_linux)
   arch->vex.guest_EIP -= 2;             // sizeof(int $0x80)

   /* Make sure our caller is actually sane, and we're really backing
      back over a syscall.

      int $0x80 == CD 80 
   */
   {
      UChar *p = (UChar *)arch->vex.guest_EIP;
      
      if (p[0] != 0xcd || p[1] != 0x80)
         VG_(message)(Vg_DebugMsg,
                      "?! restarting over syscall at %#x %02x %02x\n",
                      arch->vex.guest_EIP, p[0], p[1]); 

      vg_assert(p[0] == 0xcd && p[1] == 0x80);
   }

#elif defined(VGP_amd64_linux)
   arch->vex.guest_RIP -= 2;             // sizeof(syscall)

   /* Make sure our caller is actually sane, and we're really backing
      back over a syscall.

      syscall == 0F 05 
   */
   {
      UChar *p = (UChar *)arch->vex.guest_RIP;
      
      if (p[0] != 0x0F || p[1] != 0x05)
         VG_(message)(Vg_DebugMsg,
                      "?! restarting over syscall at %#llx %02x %02x\n",
                      arch->vex.guest_RIP, p[0], p[1]); 

      vg_assert(p[0] == 0x0F && p[1] == 0x05);
   }

#elif defined(VGP_ppc32_linux) || defined(VGP_ppc64_linux)
   arch->vex.guest_CIA -= 4;             // sizeof(ppc32 instr)

   /* Make sure our caller is actually sane, and we're really backing
      back over a syscall.

      sc == 44 00 00 02
   */
   {
      UChar *p = (UChar *)arch->vex.guest_CIA;

      if (p[0] != 0x44 || p[1] != 0x0 || p[2] != 0x0 || p[3] != 0x02)
         VG_(message)(Vg_DebugMsg,
                      "?! restarting over syscall at %#llx %02x %02x %02x %02x\n",
                      arch->vex.guest_CIA + 0ULL, p[0], p[1], p[2], p[3]);

      vg_assert(p[0] == 0x44 && p[1] == 0x0 && p[2] == 0x0 && p[3] == 0x2);
   }

#elif defined(VGP_ppc32_aix5) || defined(VGP_ppc64_aix5)
   /* Hmm.  This is problematic, because on AIX the kernel resumes
      after a syscall at LR, not at the insn following SC.  Hence
      there is no obvious way to figure out where the SC is.  Current
      solution is to have a pseudo-register in the guest state,
      CIA_AT_SC, which holds the address of the most recent SC
      executed.  Backing up to that syscall then simply involves
      copying that value back into CIA (the program counter). */
   arch->vex.guest_CIA = arch->vex.guest_CIA_AT_SC;

   /* Make sure our caller is actually sane, and we're really backing
      back over a syscall.

      sc == 44 00 00 02
   */
   {
      UChar *p = (UChar *)arch->vex.guest_CIA;

      if (p[0] != 0x44 || p[1] != 0x0 || p[2] != 0x0 || p[3] != 0x02)
         VG_(message)(Vg_DebugMsg,
                      "?! restarting over syscall at %#lx %02x %02x %02x %02x\n",
                      (UWord)arch->vex.guest_CIA, p[0], p[1], p[2], p[3]);

      vg_assert(p[0] == 0x44 && p[1] == 0x0 && p[2] == 0x0 && p[3] == 0x2);
   }

#elif defined(VGP_x86_darwin)
   arch->vex.guest_EIP -= 2;             // sizeof(int $0x80)

   /* Make sure our caller is actually sane, and we're really backing
      back over a syscall.

      int $0x80 == CD 80
      int $0x80 == CD 81
   */
   // DDD: #warning GrP fixme sysenter, int $0x81, int $0x82
   {
       UChar *p = (UChar *)arch->vex.guest_EIP;

       if (p[0] != 0xcd || (p[1] != 0x80 && p[1] != 0x81))
           VG_(message)(Vg_DebugMsg,
                        "?! restarting over syscall at %#x %02x %02x\n",
                        arch->vex.guest_EIP, p[0], p[1]);

       vg_assert(p[0] == 0xcd && (p[1] == 0x80 || p[1] == 0x81));
   }
   
#elif defined(VGP_amd64_darwin)
   // DDD: #warning GrP fixme amd64 restart unimplemented
   vg_assert(0);
   
#else
#  error "ML_(fixup_guest_state_to_restart_syscall): unknown plat"
#endif
}

/* 
   Fix up the guest state when a syscall is interrupted by a signal
   and so has been forced to return 'sysret'.

   To do this, we determine the precise state of the syscall by
   looking at the (real) IP at the time the signal happened.  The
   syscall sequence looks like:

     1. unblock signals
     2. perform syscall
     3. save result to guest state (EAX, RAX, R3+CR0.SO)
     4. re-block signals

   If a signal
   happens at      Then     Why?
   [1-2)           restart  nothing has happened (restart syscall)
   [2]             restart  syscall hasn't started, or kernel wants to restart
   [2-3)           save     syscall complete, but results not saved
   [3-4)           syscall complete, results saved

   Sometimes we never want to restart an interrupted syscall (because
   sigaction says not to), so we only restart if "restart" is True.

   This will also call VG_(post_syscall) if the syscall has actually
   completed (either because it was interrupted, or because it
   actually finished).  It will not call VG_(post_syscall) if the
   syscall is set up for restart, which means that the pre-wrapper may
   get called multiple times.
*/

void 
VG_(fixup_guest_state_after_syscall_interrupted)( ThreadId tid, 
                                                  Addr     ip, 
                                                  UWord    sysnum, 
                                                  SysRes   sres,
                                                  Bool     restart)
{
   /* Note that the sysnum arg seems to contain not-dependable-on info
      (I think it depends on the state the real syscall was in at
      interrupt) and so is ignored, apart from in the following
      printf. */

   static const Bool debug = False;

   ThreadState*     tst;
   SyscallStatus    canonical;
   ThreadArchState* th_regs;
   SyscallInfo*     sci;

   if (debug)
      VG_(printf)( "interrupted_syscall %d: tid=%d, IP=0x%llx, "
                   "restart=%s, sysret.isError=%s, sysret.val=%lld\n", 
                   (Int)sysnum,
                   (Int)tid,
                   (ULong)ip, 
                   restart ? "True" : "False", 
                   sres.isError ? "True" : "False",
                   (Long)(Word)(sres.isError ? sres.err : sres.res) );

   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(tid >= 1 && tid < VG_N_THREADS);
   vg_assert(VG_(is_running_thread)(tid));

   tst     = VG_(get_ThreadState)(tid);
   th_regs = &tst->arch;
   sci     = & syscallInfo[tid];

   /* Figure out what the state of the syscall was by examining the
      (real) IP at the time of the signal, and act accordingly. */

   if (ip < ML_(blksys_setup) || ip >= ML_(blksys_finished)) {
      VG_(printf)("  not in syscall (%#lx - %#lx)\n",
                  ML_(blksys_setup), ML_(blksys_finished));
      /* Looks like we weren't in a syscall at all.  Hmm. */
      vg_assert(sci->status.what != SsIdle);
      return;
   }

   /* We should not be here unless this thread had first started up
      the machinery for a syscall by calling VG_(client_syscall).
      Hence: */
   vg_assert(sci->status.what != SsIdle);

   if (ip >= ML_(blksys_setup) && ip < ML_(blksys_restart)) {
      /* syscall hasn't even started; go around again */
      if (debug)
         VG_(printf)("  not started: restart\n");
      vg_assert(sci->status.what == SsHandToKernel);
      ML_(fixup_guest_state_to_restart_syscall)(th_regs);
   } 

   else 
   if (ip == ML_(blksys_restart)) {
      /* We're either about to run the syscall, or it was interrupted
         and the kernel restarted it.  Restart if asked, otherwise
         EINTR it. */
      if (restart)
         ML_(fixup_guest_state_to_restart_syscall)(th_regs);
      else {
         canonical = convert_SysRes_to_SyscallStatus( 
                        VG_(mk_SysRes_Error)( VKI_EINTR ) 
                     );
         if (!(sci->flags & SfNoWriteResult))
            putSyscallStatusIntoGuestState( tid, &canonical, &th_regs->vex );
         sci->status = canonical;
         VG_(post_syscall)(tid);
      }
   }

   else 
   if (ip >= ML_(blksys_complete) && ip < ML_(blksys_committed)) {
      /* Syscall complete, but result hasn't been written back yet.
         Write the SysRes we were supplied with back to the guest
         state. */
      if (debug)
         VG_(printf)("  completed\n");
      canonical = convert_SysRes_to_SyscallStatus( sres );
      if (!(sci->flags & SfNoWriteResult))
         putSyscallStatusIntoGuestState( tid, &canonical, &th_regs->vex );
      sci->status = canonical;
      VG_(post_syscall)(tid);
   } 

   else 
   if (ip >= ML_(blksys_committed) && ip < ML_(blksys_finished)) {
      /* Result committed, but the signal mask has not been restored;
         we expect our caller (the signal handler) will have fixed
         this up. */
      if (debug)
         VG_(printf)("  all done\n");
      VG_(post_syscall)(tid);
   } 

   else
      VG_(core_panic)("?? strange syscall interrupt state?");

   /* In all cases, the syscall is now finished (even if we called
      ML_(fixup_guest_state_to_restart_syscall), since that just
      re-positions the guest's IP for another go at it).  So we need
      to record that fact. */
   sci->status.what = SsIdle;
}


#if defined(VGO_darwin)
// Clean up after workq_ops(WQOPS_THREAD_RETURN) jumped to wqthread_hijack. 
// This is similar to VG_(fixup_guest_state_after_syscall_interrupted).
// This longjmps back to the scheduler.
void ML_(wqthread_continue_NORETURN)(ThreadId tid)
{
   ThreadState*     tst;
   SyscallInfo*     sci;

   VG_(acquire_BigLock)(tid, "wqthread_continue");

   PRINT("SYSCALL[%d,%d](%5lld) workq_ops() starting new workqueue item\n", 
         VG_(getpid)(), tid, (Long)VG_DARWIN_SYSNO_PRINT(__NR_workq_ops));

   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(tid >= 1 && tid < VG_N_THREADS);
   vg_assert(VG_(is_running_thread)(tid));

   tst     = VG_(get_ThreadState)(tid);
   sci     = & syscallInfo[tid];
   vg_assert(sci->status.what != SsIdle);
   vg_assert(tst->os_state.wq_jmpbuf_valid);  // check this BEFORE post_syscall

   // Pretend the syscall completed normally, but don't touch the thread state.
   sci->status = convert_SysRes_to_SyscallStatus( VG_(mk_SysRes_Success)(0) );
   sci->flags |= SfNoWriteResult;
   VG_(post_syscall)(tid);

   sci->status.what = SsIdle;

   vg_assert(tst->sched_jmpbuf_valid);
   __builtin_longjmp(tst->sched_jmpbuf, True);

   /* NOTREACHED */
   vg_assert(0);
}
#endif


/* ---------------------------------------------------------------------
   A place to store the where-to-call-when-really-done pointer
   ------------------------------------------------------------------ */

// When the final thread is done, where shall I call to shutdown the
// system cleanly?  Is set once at startup (in m_main) and never
// changes after that.  Is basically a pointer to the exit
// continuation.  This is all just a nasty hack to avoid calling
// directly from m_syswrap to m_main at exit, since that would cause
// m_main to become part of a module cycle, which is silly.
void (* VG_(address_of_m_main_shutdown_actions_NORETURN) )
       (ThreadId,VgSchedReturnCode)
   = NULL;

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
