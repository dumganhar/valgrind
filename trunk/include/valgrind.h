/* -*- c -*-
   ----------------------------------------------------------------

   Notice that the following BSD-style license applies to this one
   file (valgrind.h) only.  The entire rest of Valgrind is licensed
   under the terms of the GNU General Public License, version 2.  See
   the COPYING file in the source distribution for details.

   ----------------------------------------------------------------

   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2005 Julian Seward.  All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

   2. The origin of this software must not be misrepresented; you must 
      not claim that you wrote the original software.  If you use this 
      software in a product, an acknowledgment in the product 
      documentation would be appreciated but is not required.

   3. Altered source versions must be plainly marked as such, and must
      not be misrepresented as being the original software.

   4. The name of the author may not be used to endorse or promote 
      products derived from this software without specific prior written 
      permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
   OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   ----------------------------------------------------------------

   Notice that the above BSD-style license applies to this one file
   (valgrind.h) only.  The entire rest of Valgrind is licensed under
   the terms of the GNU General Public License, version 2.  See the
   COPYING file in the source distribution for details.

   ---------------------------------------------------------------- 
*/


/* This file is for inclusion into client (your!) code.

   You can use these macros to manipulate and query Valgrind's 
   execution inside your own programs.

   The resulting executables will still run without Valgrind, just a
   little bit more slowly than they otherwise would, but otherwise
   unchanged.  When not running on valgrind, each client request
   consumes very few (eg. 7) instructions, so the resulting performance
   loss is negligible unless you plan to execute client requests
   millions of times per second.  Nevertheless, if that is still a
   problem, you can compile with the NVALGRIND symbol defined (gcc
   -DNVALGRIND) so that client requests are not even compiled in.  */

#ifndef __VALGRIND_H
#define __VALGRIND_H

#include <stdarg.h>

/* Nb: this file might be included in a file compiled with -ansi.  So
   we can't use C++ style "//" comments nor the "asm" keyword (instead
   use "__asm__"). */

/* Derive some tags indicating what the target architecture is.  Note
   that in this file we're using the compiler's CPP symbols for
   identifying architectures, which are different to the ones we use
   within the rest of Valgrind.  Note, __powerpc__ is active for both
   32 and 64-bit PPC, whereas __powerpc64__ is only active for the
   latter. */
#undef ARCH_x86
#undef ARCH_amd64
#undef ARCH_ppc32
#undef ARCH_ppc64

#if defined(__i386__)
#  define ARCH_x86 1
#elif defined(__x86_64__)
#  define ARCH_amd64 1
#elif defined(__powerpc__) && !defined(__powerpc64__)
#  define ARCH_ppc32 1
#elif defined(__powerpc__) && defined(__powerpc64__)
#  define ARCH_ppc64 1
#endif

/* If we're not compiling for our target architecture, don't generate
   any inline asms.  */
#if !defined(ARCH_x86) && !defined(ARCH_amd64) \
    && !defined(ARCH_ppc32) && !defined(ARCH_ppc64)
#  if !defined(NVALGRIND)
#    define NVALGRIND 1
#  endif
#endif


/* ------------------------------------------------------------------ */
/* ARCHITECTURE SPECIFICS for SPECIAL INSTRUCTIONS.  There is nothing */
/* in here of use to end-users -- skip to the next section.           */
/* ------------------------------------------------------------------ */

#if defined(NVALGRIND)

/* Define NVALGRIND to completely remove the Valgrind magic sequence
   from the compiled code (analogous to NDEBUG's effects on
   assert()) */
#define VALGRIND_DO_CLIENT_REQUEST(                               \
        _zzq_rlval, _zzq_default, _zzq_request,                   \
        _zzq_arg1, _zzq_arg2, _zzq_arg3, _zzq_arg4)               \
   {                                                              \
      (_zzq_rlval) = (_zzq_default);                              \
   }

#else  /* ! NVALGRIND */

/* The following defines the magic code sequences which the JITter
   spots and handles magically.  Don't look too closely at them as
   they will rot your brain.

   The assembly code sequences for all architectures is in this one
   file.  This is because this file must be stand-alone, and we don't
   want to have multiple files.

   For VALGRIND_DO_CLIENT_REQUEST, we must ensure that the default
   value gets put in the return slot, so that everything works when
   this is executed not under Valgrind.  Args are passed in a memory
   block, and so there's no intrinsic limit to the number that could
   be passed, but it's currently four.
   
   The macro args are: 
      _zzq_rlval    result lvalue
      _zzq_default  default value (result returned when running on real CPU)
      _zzq_request  request code
      _zzq_arg1..4  request params

   The other two macros are used to support function wrapping, and are
   a lot simpler.  VALGRIND_GET_NR_CONTEXT returns the value of the
   guest's NRADDR pseudo-register and whatever other information is
   needed to safely run the call original from the wrapper: on
   ppc64-linux, the R2 value at the divert point is also needed.  This
   information is abstracted into a user-visible type, OrigFn.

   VALGRIND_CALL_NOREDIR_* behaves the same as the following on the
   guest, but guarantees that the branch instruction will not be
   redirected: x86: call *%eax, amd64: call *%rax, ppc32/ppc64:
   branch-and-link-to-r11.  VALGRIND_CALL_NOREDIR is just text, not a
   complete inline asm, since it needs to be combined with more magic
   inline asm stuff to be useful.
*/

/* ---------------------------- x86 ---------------------------- */

#if defined(ARCH_x86)

typedef
   struct { 
      unsigned int nraddr; /* where's the code? */
   }
   OrigFn;

#define __SPECIAL_INSTRUCTION_PREAMBLE                            \
                     "roll $3,  %%edi ; roll $13, %%edi\n\t"      \
                     "roll $29, %%edi ; roll $19, %%edi\n\t"

#define VALGRIND_DO_CLIENT_REQUEST(                               \
        _zzq_rlval, _zzq_default, _zzq_request,                   \
        _zzq_arg1, _zzq_arg2, _zzq_arg3, _zzq_arg4)               \
  { volatile unsigned int _zzq_args[5];                           \
    volatile unsigned int _zzq_result;                            \
    _zzq_args[0] = (unsigned int)(_zzq_request);                  \
    _zzq_args[1] = (unsigned int)(_zzq_arg1);                     \
    _zzq_args[2] = (unsigned int)(_zzq_arg2);                     \
    _zzq_args[3] = (unsigned int)(_zzq_arg3);                     \
    _zzq_args[4] = (unsigned int)(_zzq_arg4);                     \
    __asm__ volatile(__SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* %EDX = client_request ( %EAX ) */         \
                     "xchgl %%ebx,%%ebx"                          \
                     : "=d" (_zzq_result)                         \
                     : "a" (&_zzq_args[0]), "0" (_zzq_default)    \
                     : "cc", "memory"                             \
                    );                                            \
    _zzq_rlval = _zzq_result;                                     \
  }

#define VALGRIND_GET_NR_CONTEXT(_zzq_rlval)                       \
  { volatile OrigFn* _zzq_orig = &(_zzq_rlval);                   \
    volatile unsigned int __addr;                                 \
    __asm__ volatile(__SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* %EAX = guest_NRADDR */                    \
                     "xchgl %%ecx,%%ecx"                          \
                     : "=a" (__addr)                              \
                     :                                            \
                     : "cc", "memory"                             \
                    );                                            \
    _zzq_orig->nraddr = __addr;                                   \
  }

#define VALGRIND_CALL_NOREDIR_EAX                                 \
                     __SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* call-noredir *%EAX */                     \
                     "xchgl %%edx,%%edx\n\t"
#endif /* ARCH_x86 */

/* --------------------------- amd64 --------------------------- */

#if defined(ARCH_amd64)

typedef
   struct { 
      unsigned long long int nraddr; /* where's the code? */
   }
   OrigFn;

#define __SPECIAL_INSTRUCTION_PREAMBLE                            \
                     "rolq $3,  %%rdi ; rolq $13, %%rdi\n\t"      \
                     "rolq $61, %%rdi ; rolq $51, %%rdi\n\t"

#define VALGRIND_DO_CLIENT_REQUEST(                               \
        _zzq_rlval, _zzq_default, _zzq_request,                   \
        _zzq_arg1, _zzq_arg2, _zzq_arg3, _zzq_arg4)               \
  { volatile unsigned long long int _zzq_args[5];                 \
    volatile unsigned long long int _zzq_result;                  \
    _zzq_args[0] = (unsigned long long int)(_zzq_request);        \
    _zzq_args[1] = (unsigned long long int)(_zzq_arg1);           \
    _zzq_args[2] = (unsigned long long int)(_zzq_arg2);           \
    _zzq_args[3] = (unsigned long long int)(_zzq_arg3);           \
    _zzq_args[4] = (unsigned long long int)(_zzq_arg4);           \
    __asm__ volatile(__SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* %RDX = client_request ( %RAX ) */         \
                     "xchgq %%rbx,%%rbx"                          \
                     : "=d" (_zzq_result)                         \
                     : "a" (&_zzq_args[0]), "0" (_zzq_default)    \
                     : "cc", "memory"                             \
                    );                                            \
    _zzq_rlval = _zzq_result;                                     \
  }

#define VALGRIND_GET_NR_CONTEXT(_zzq_rlval)                       \
  { volatile OrigFn* _zzq_orig = &(_zzq_rlval);                   \
    volatile unsigned long long int __addr;                       \
    __asm__ volatile(__SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* %RAX = guest_NRADDR */                    \
                     "xchgq %%rcx,%%rcx"                          \
                     : "=a" (__addr)                              \
                     :                                            \
                     : "cc", "memory"                             \
                    );                                            \
    _zzq_orig->nraddr = __addr;                                   \
  }

#define VALGRIND_CALL_NOREDIR_RAX                                 \
                     __SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* call-noredir *%RAX */                     \
                     "xchgq %%rdx,%%rdx\n\t"
#endif /* ARCH_amd64 */

/* --------------------------- ppc32 --------------------------- */

#if defined(ARCH_ppc32)

typedef
   struct { 
      unsigned int nraddr; /* where's the code? */
   }
   OrigFn;

#define __SPECIAL_INSTRUCTION_PREAMBLE                            \
                     "rlwinm 0,0,3,0,0  ; rlwinm 0,0,13,0,0\n\t"  \
                     "rlwinm 0,0,29,0,0 ; rlwinm 0,0,19,0,0\n\t"

#define VALGRIND_DO_CLIENT_REQUEST(                               \
        _zzq_rlval, _zzq_default, _zzq_request,                   \
        _zzq_arg1, _zzq_arg2, _zzq_arg3, _zzq_arg4)               \
                                                                  \
  {          unsigned int  _zzq_args[5];                          \
    register unsigned int  _zzq_result __asm__("r3");             \
    register unsigned int* _zzq_ptr __asm__("r4");                \
    _zzq_args[0] = (unsigned int)(_zzq_request);                  \
    _zzq_args[1] = (unsigned int)(_zzq_arg1);                     \
    _zzq_args[2] = (unsigned int)(_zzq_arg2);                     \
    _zzq_args[3] = (unsigned int)(_zzq_arg3);                     \
    _zzq_args[4] = (unsigned int)(_zzq_arg4);                     \
    _zzq_ptr = _zzq_args;                                         \
    __asm__ volatile(__SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* %R3 = client_request ( %R4 ) */           \
                     "or 1,1,1"                                   \
                     : "=r" (_zzq_result)                         \
                     : "0" (_zzq_default), "r" (_zzq_ptr)         \
                     : "cc", "memory");                           \
    _zzq_rlval = _zzq_result;                                     \
  }

#define VALGRIND_GET_NR_CONTEXT(_zzq_rlval)                       \
  { volatile OrigFn* _zzq_orig = &(_zzq_rlval);                   \
    register unsigned int __addr __asm__("r3");                   \
    __asm__ volatile(__SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* %R3 = guest_NRADDR */                     \
                     "or 2,2,2"                                   \
                     : "=r" (__addr)                              \
                     :                                            \
                     : "cc", "memory"                             \
                    );                                            \
    _zzq_orig->nraddr = __addr;                                   \
  }

#define VALGRIND_BRANCH_AND_LINK_TO_NOREDIR_R11                   \
                     __SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* branch-and-link-to-noredir *%R11 */       \
                     "or 3,3,3\n\t"
#endif /* ARCH_ppc32 */

/* --------------------------- ppc64 --------------------------- */

#if defined(ARCH_ppc64)

typedef
   struct { 
      unsigned long long int nraddr; /* where's the code? */
      unsigned long long int r2;  /* what tocptr do we need? */
   }
   OrigFn;

#define __SPECIAL_INSTRUCTION_PREAMBLE                            \
                     "rotldi 0,0,3  ; rotldi 0,0,13\n\t"          \
                     "rotldi 0,0,61 ; rotldi 0,0,51\n\t"

#define VALGRIND_DO_CLIENT_REQUEST(                               \
        _zzq_rlval, _zzq_default, _zzq_request,                   \
        _zzq_arg1, _zzq_arg2, _zzq_arg3, _zzq_arg4)               \
                                                                  \
  {          unsigned long long int  _zzq_args[5];                \
    register unsigned long long int  _zzq_result __asm__("r3");   \
    register unsigned long long int* _zzq_ptr __asm__("r4");      \
    _zzq_args[0] = (unsigned long long int)(_zzq_request);        \
    _zzq_args[1] = (unsigned long long int)(_zzq_arg1);           \
    _zzq_args[2] = (unsigned long long int)(_zzq_arg2);           \
    _zzq_args[3] = (unsigned long long int)(_zzq_arg3);           \
    _zzq_args[4] = (unsigned long long int)(_zzq_arg4);           \
    _zzq_ptr = _zzq_args;                                         \
    __asm__ volatile(__SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* %R3 = client_request ( %R4 ) */           \
                     "or 1,1,1"                                   \
                     : "=r" (_zzq_result)                         \
                     : "0" (_zzq_default), "r" (_zzq_ptr)         \
                     : "cc", "memory");                           \
    _zzq_rlval = _zzq_result;                                     \
  }

#define VALGRIND_GET_NR_CONTEXT(_zzq_rlval)                       \
  { volatile OrigFn* _zzq_orig = &(_zzq_rlval);                   \
    register unsigned long long int __addr __asm__("r3");         \
    __asm__ volatile(__SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* %R3 = guest_NRADDR */                     \
                     "or 2,2,2"                                   \
                     : "=r" (__addr)                              \
                     :                                            \
                     : "cc", "memory"                             \
                    );                                            \
    _zzq_orig->nraddr = __addr;                                   \
    __asm__ volatile(__SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* %R3 = guest_NRADDR_GPR2 */                \
                     "or 4,4,4"                                   \
                     : "=r" (__addr)                              \
                     :                                            \
                     : "cc", "memory"                             \
                    );                                            \
    _zzq_orig->r2 = __addr;                                       \
  }

#define VALGRIND_BRANCH_AND_LINK_TO_NOREDIR_R11                   \
                     __SPECIAL_INSTRUCTION_PREAMBLE               \
                     /* branch-and-link-to-noredir *%R11 */       \
                     "or 3,3,3\n\t"

#endif /* ARCH_ppc64 */

/* Insert assembly code for other architectures here... */

#endif /* NVALGRIND */


/* ------------------------------------------------------------------ */
/* ARCHITECTURE SPECIFICS for FUNCTION WRAPPING.  This is all very    */
/* ugly.  It's the least-worst tradeoff I can think of.               */
/* ------------------------------------------------------------------ */

/* This section defines magic (a.k.a appalling-hack) macros for doing
   guaranteed-no-redirection macros, so as to get from function
   wrappers to the functions they are wrapping.  The whole point is to
   construct standard call sequences, but to do the call itself with a
   special no-redirect call pseudo-instruction that the JIT
   understands and handles specially.  This section is long and
   repetitious, and I can't see a way to make it shorter.

   The naming scheme is as follows:

      CALL_FN_{W,v}_{v,W,WW,WWW,WWWW,5W,6W,7W,etc}

   'W' stands for "word" and 'v' for "void".  Hence there are
   different macros for calling arity 0, 1, 2, 3, 4, etc, functions,
   and for each, the possibility of returning a word-typed result, or
   no result.
*/

/* Use these to write the name of your wrapper.  NOTE: duplicates
   VG_WRAP_FUNCTION_Z{U,Z} in pub_tool_redir.h. */

#define I_WRAP_SONAME_FNNAME_ZU(soname,fnname)                    \
   _vgwZU_##soname##_##fnname

#define I_WRAP_SONAME_FNNAME_ZZ(soname,fnname)                    \
   _vgwZZ_##soname##_##fnname

/* Use this macro from within a wrapper function to collect the
   context (address and possibly other info) of the original function.
   Once you have that you can then use it in one of the CALL_FN_
   macros.  The type of the argument _lval is OrigFn. */
#define VALGRIND_GET_ORIG_FN(_lval)  VALGRIND_GET_NR_CONTEXT(_lval)

/* Derivatives of the main macros below, for calling functions
   returning void. */

#define CALL_FN_v_v(fnptr)                                        \
   do { volatile unsigned long _junk;                             \
        CALL_FN_W_v(_junk,fnptr); } while (0)

#define CALL_FN_v_W(fnptr, arg1)                                  \
   do { volatile unsigned long _junk;                             \
        CALL_FN_W_W(_junk,fnptr,arg1); } while (0)

#define CALL_FN_v_WW(fnptr, arg1,arg2)                            \
   do { volatile unsigned long _junk;                             \
        CALL_FN_W_WW(_junk,fnptr,arg1,arg2); } while (0)

/* ---------------------------- x86 ---------------------------- */

#if defined(ARCH_x86)

/* These regs are trashed by the hidden call.  No need to mention eax
   as gcc can already see that, plus causes gcc to bomb. */
#define __CALLER_SAVED_REGS /*"eax"*/ "ecx", "edx"

/* These CALL_FN_ macros assume that on x86-linux, sizeof(unsigned
   long) == 4. */

#define CALL_FN_W_v(lval, orig)                                   \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[1];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      __asm__ volatile(                                           \
         "movl (%%eax), %%eax\n\t"  /* target->%eax */            \
         VALGRIND_CALL_NOREDIR_EAX                                \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_W(lval, orig, arg1)                             \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[2];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)(arg1);                         \
      __asm__ volatile(                                           \
         "pushl 4(%%eax)\n\t"                                     \
         "movl (%%eax), %%eax\n\t"  /* target->%eax */            \
         VALGRIND_CALL_NOREDIR_EAX                                \
         "addl $4, %%esp\n"                                       \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_WW(lval, orig, arg1,arg2)                       \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[3];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)(arg1);                         \
      _argvec[2] = (unsigned long)(arg2);                         \
      __asm__ volatile(                                           \
         "pushl 8(%%eax)\n\t"                                     \
         "pushl 4(%%eax)\n\t"                                     \
         "movl (%%eax), %%eax\n\t"  /* target->%eax */            \
         VALGRIND_CALL_NOREDIR_EAX                                \
         "addl $8, %%esp\n"                                       \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_WWWW(lval, orig, arg1,arg2,arg3,arg4)           \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[5];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)(arg1);                         \
      _argvec[2] = (unsigned long)(arg2);                         \
      _argvec[3] = (unsigned long)(arg3);                         \
      _argvec[4] = (unsigned long)(arg4);                         \
      __asm__ volatile(                                           \
         "pushl 16(%%eax)\n\t"                                    \
         "pushl 12(%%eax)\n\t"                                    \
         "pushl 8(%%eax)\n\t"                                     \
         "pushl 4(%%eax)\n\t"                                     \
         "movl (%%eax), %%eax\n\t"  /* target->%eax */            \
         VALGRIND_CALL_NOREDIR_EAX                                \
         "addl $16, %%esp\n"                                      \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_5W(lval, orig, arg1,arg2,arg3,arg4,arg5)        \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[6];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)(arg1);                         \
      _argvec[2] = (unsigned long)(arg2);                         \
      _argvec[3] = (unsigned long)(arg3);                         \
      _argvec[4] = (unsigned long)(arg4);                         \
      _argvec[5] = (unsigned long)(arg5);                         \
      __asm__ volatile(                                           \
         "pushl 20(%%eax)\n\t"                                    \
         "pushl 16(%%eax)\n\t"                                    \
         "pushl 12(%%eax)\n\t"                                    \
         "pushl 8(%%eax)\n\t"                                     \
         "pushl 4(%%eax)\n\t"                                     \
         "movl (%%eax), %%eax\n\t"  /* target->%eax */            \
         VALGRIND_CALL_NOREDIR_EAX                                \
         "addl $20, %%esp\n"                                      \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_6W(lval, orig, arg1,arg2,arg3,arg4,arg5,arg6)   \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[7];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)(arg1);                         \
      _argvec[2] = (unsigned long)(arg2);                         \
      _argvec[3] = (unsigned long)(arg3);                         \
      _argvec[4] = (unsigned long)(arg4);                         \
      _argvec[5] = (unsigned long)(arg5);                         \
      _argvec[6] = (unsigned long)(arg6);                         \
      __asm__ volatile(                                           \
         "pushl 24(%%eax)\n\t"                                    \
         "pushl 20(%%eax)\n\t"                                    \
         "pushl 16(%%eax)\n\t"                                    \
         "pushl 12(%%eax)\n\t"                                    \
         "pushl 8(%%eax)\n\t"                                     \
         "pushl 4(%%eax)\n\t"                                     \
         "movl (%%eax), %%eax\n\t"  /* target->%eax */            \
         VALGRIND_CALL_NOREDIR_EAX                                \
         "addl $24, %%esp\n"                                      \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_7W(lval, orig, arg1,arg2,arg3,arg4,arg5,arg6,   \
                                 arg7)                            \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[8];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)(arg1);                         \
      _argvec[2] = (unsigned long)(arg2);                         \
      _argvec[3] = (unsigned long)(arg3);                         \
      _argvec[4] = (unsigned long)(arg4);                         \
      _argvec[5] = (unsigned long)(arg5);                         \
      _argvec[6] = (unsigned long)(arg6);                         \
      _argvec[7] = (unsigned long)(arg7);                         \
      __asm__ volatile(                                           \
         "pushl 28(%%eax)\n\t"                                    \
         "pushl 24(%%eax)\n\t"                                    \
         "pushl 20(%%eax)\n\t"                                    \
         "pushl 16(%%eax)\n\t"                                    \
         "pushl 12(%%eax)\n\t"                                    \
         "pushl 8(%%eax)\n\t"                                     \
         "pushl 4(%%eax)\n\t"                                     \
         "movl (%%eax), %%eax\n\t"  /* target->%eax */            \
         VALGRIND_CALL_NOREDIR_EAX                                \
         "addl $28, %%esp\n"                                      \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_8W(lval, orig, arg1,arg2,arg3,arg4,arg5,arg6,   \
                                 arg7,arg8)                       \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[9];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)(arg1);                         \
      _argvec[2] = (unsigned long)(arg2);                         \
      _argvec[3] = (unsigned long)(arg3);                         \
      _argvec[4] = (unsigned long)(arg4);                         \
      _argvec[5] = (unsigned long)(arg5);                         \
      _argvec[6] = (unsigned long)(arg6);                         \
      _argvec[7] = (unsigned long)(arg7);                         \
      _argvec[8] = (unsigned long)(arg8);                         \
      __asm__ volatile(                                           \
         "pushl 32(%%eax)\n\t"                                    \
         "pushl 28(%%eax)\n\t"                                    \
         "pushl 24(%%eax)\n\t"                                    \
         "pushl 20(%%eax)\n\t"                                    \
         "pushl 16(%%eax)\n\t"                                    \
         "pushl 12(%%eax)\n\t"                                    \
         "pushl 8(%%eax)\n\t"                                     \
         "pushl 4(%%eax)\n\t"                                     \
         "movl (%%eax), %%eax\n\t"  /* target->%eax */            \
         VALGRIND_CALL_NOREDIR_EAX                                \
         "addl $32, %%esp\n"                                      \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_12W(lval, orig, arg1,arg2,arg3,arg4,arg5,       \
                                  arg6,arg7,arg8,arg9,arg10,      \
                                  arg11,arg12)                    \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[13];                         \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)(arg1);                         \
      _argvec[2] = (unsigned long)(arg2);                         \
      _argvec[3] = (unsigned long)(arg3);                         \
      _argvec[4] = (unsigned long)(arg4);                         \
      _argvec[5] = (unsigned long)(arg5);                         \
      _argvec[6] = (unsigned long)(arg6);                         \
      _argvec[7] = (unsigned long)(arg7);                         \
      _argvec[8] = (unsigned long)(arg8);                         \
      _argvec[9] = (unsigned long)(arg9);                         \
      _argvec[10] = (unsigned long)(arg10);                       \
      _argvec[11] = (unsigned long)(arg11);                       \
      _argvec[12] = (unsigned long)(arg12);                       \
      __asm__ volatile(                                           \
         "pushl 48(%%eax)\n\t"                                    \
         "pushl 44(%%eax)\n\t"                                    \
         "pushl 40(%%eax)\n\t"                                    \
         "pushl 36(%%eax)\n\t"                                    \
         "pushl 32(%%eax)\n\t"                                    \
         "pushl 28(%%eax)\n\t"                                    \
         "pushl 24(%%eax)\n\t"                                    \
         "pushl 20(%%eax)\n\t"                                    \
         "pushl 16(%%eax)\n\t"                                    \
         "pushl 12(%%eax)\n\t"                                    \
         "pushl 8(%%eax)\n\t"                                     \
         "pushl 4(%%eax)\n\t"                                     \
         "movl (%%eax), %%eax\n\t"  /* target->%eax */            \
         VALGRIND_CALL_NOREDIR_EAX                                \
         "addl $48, %%esp\n"                                      \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#endif /* ARCH_x86 */

/* --------------------------- amd64 --------------------------- */

#if defined(ARCH_amd64)

/* ARGREGS: rdi rsi rdx rcx r8 r9 (the rest on stack in R-to-L order) */

/* These regs are trashed by the hidden call. */
#define __CALLER_SAVED_REGS /*"rax",*/ "rcx", "rdx", "rsi",       \
                            "rdi", "r8", "r9", "r10", "r11"

/* These CALL_FN_ macros assume that on amd64-linux, sizeof(unsigned
   long) == 8. */

#define CALL_FN_W_v(lval, orig)                                   \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[1];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      __asm__ volatile(                                           \
         "movq (%%rax), %%rax\n\t"  /* target->%rax */            \
         VALGRIND_CALL_NOREDIR_RAX                                \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_W(lval, orig, arg1)                             \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[2];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)(arg1);                         \
      __asm__ volatile(                                           \
         "movq 8(%%rax), %%rdi\n\t"                               \
         "movq (%%rax), %%rax\n\t"  /* target->%rax */            \
         VALGRIND_CALL_NOREDIR_RAX                                \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_WW(lval, orig, arg1,arg2)                       \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[3];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)(arg1);                         \
      _argvec[2] = (unsigned long)(arg2);                         \
      __asm__ volatile(                                           \
         "movq 16(%%rax), %%rsi\n\t"                              \
         "movq 8(%%rax), %%rdi\n\t"                               \
         "movq (%%rax), %%rax\n\t"  /* target->%rax */            \
         VALGRIND_CALL_NOREDIR_RAX                                \
         : /*out*/   "=a" (_res)                                  \
         : /*in*/    "a" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#endif /* ARCH_amd64 */

/* --------------------------- ppc32 --------------------------- */

#if defined(ARCH_ppc32)

/* ARGREGS: r3 r4 r5 r6 r7 r8 r9 r10 (the rest on stack somewhere) */

/* These regs are trashed by the hidden call. */
#define __CALLER_SAVED_REGS "lr",                                 \
                            "r0", "r2", "r3", "r4", "r5", "r6",   \
                            "r7", "r8", "r9", "r10", "r11", "r12"

/* These CALL_FN_ macros assume that on ppc32-linux, sizeof(unsigned
   long) == 4. */

#define CALL_FN_W_v(lval, orig)                                   \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[1];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      __asm__ volatile(                                           \
         "mr 11,%1\n\t"                                           \
         "lwz 11,0(11)\n\t"  /* target->r11 */                    \
         VALGRIND_BRANCH_AND_LINK_TO_NOREDIR_R11                  \
         "mr %0,3"                                                \
         : /*out*/   "=r" (_res)                                  \
         : /*in*/    "r" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_W(lval, orig, arg1)                             \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[2];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)arg1;                           \
      __asm__ volatile(                                           \
         "mr 11,%1\n\t"                                           \
         "lwz 3,4(11)\n\t"   /* arg1->r3 */                       \
         "lwz 11,0(11)\n\t"  /* target->r11 */                    \
         VALGRIND_BRANCH_AND_LINK_TO_NOREDIR_R11                  \
         "mr %0,3"                                                \
         : /*out*/   "=r" (_res)                                  \
         : /*in*/    "r" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_WW(lval, orig, arg1,arg2)                       \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[3];                          \
      volatile unsigned long _res;                                \
      _argvec[0] = (unsigned long)_orig.nraddr;                   \
      _argvec[1] = (unsigned long)arg1;                           \
      _argvec[2] = (unsigned long)arg2;                           \
      __asm__ volatile(                                           \
         "mr 11,%1\n\t"                                           \
         "lwz 3,4(11)\n\t"   /* arg1->r3 */                       \
         "lwz 4,8(11)\n\t"                                        \
         "lwz 11,0(11)\n\t"  /* target->r11 */                    \
         VALGRIND_BRANCH_AND_LINK_TO_NOREDIR_R11                  \
         "mr %0,3"                                                \
         : /*out*/   "=r" (_res)                                  \
         : /*in*/    "r" (&_argvec[0])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#endif /* ARCH_ppc32 */

/* --------------------------- ppc64 --------------------------- */

#if defined(ARCH_ppc64)

/* ARGREGS: r3 r4 r5 r6 r7 r8 r9 r10 (the rest on stack somewhere) */

/* These regs are trashed by the hidden call. */
#define __CALLER_SAVED_REGS "lr",                                 \
                            "r0", "r2", "r3", "r4", "r5", "r6",   \
                            "r7", "r8", "r9", "r10", "r11", "r12"

/* These CALL_FN_ macros assume that on ppc64-linux, sizeof(unsigned
   long) == 8. */

#define CALL_FN_W_v(lval, orig)                                   \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[3+0];                        \
      volatile unsigned long _res;                                \
      /* _argvec[0] holds current r2 across the call */           \
      _argvec[1] = (unsigned long)_orig.r2;                       \
      _argvec[2] = (unsigned long)_orig.nraddr;                   \
      __asm__ volatile(                                           \
         "mr 11,%1\n\t"                                           \
         "std 2,-16(11)\n\t"  /* save tocptr */                   \
         "ld   2,-8(11)\n\t"  /* use nraddr's tocptr */           \
         "ld  11, 0(11)\n\t"  /* target->r11 */                   \
         VALGRIND_BRANCH_AND_LINK_TO_NOREDIR_R11                  \
         "mr 11,%1\n\t"                                           \
         "mr %0,3\n\t"                                            \
         "ld 2,-16(11)" /* restore tocptr */                      \
         : /*out*/   "=r" (_res)                                  \
         : /*in*/    "r" (&_argvec[2])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_W(lval, orig, arg1)                             \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[3+1];                        \
      volatile unsigned long _res;                                \
      /* _argvec[0] holds current r2 across the call */           \
      _argvec[1]   = (unsigned long)_orig.r2;                     \
      _argvec[2]   = (unsigned long)_orig.nraddr;                 \
      _argvec[2+1] = (unsigned long)arg1;                         \
      __asm__ volatile(                                           \
         "mr 11,%1\n\t"                                           \
         "std 2,-16(11)\n\t"  /* save tocptr */                   \
         "ld   2,-8(11)\n\t"  /* use nraddr's tocptr */           \
         "ld   3, 8(11)\n\t"  /* arg1->r3 */                      \
         "ld  11, 0(11)\n\t"  /* target->r11 */                   \
         VALGRIND_BRANCH_AND_LINK_TO_NOREDIR_R11                  \
         "mr 11,%1\n\t"                                           \
         "mr %0,3\n\t"                                            \
         "ld 2,-16(11)" /* restore tocptr */                      \
         : /*out*/   "=r" (_res)                                  \
         : /*in*/    "r" (&_argvec[2])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#define CALL_FN_W_WW(lval, orig, arg1,arg2)                       \
   do {                                                           \
      volatile OrigFn        _orig = (orig);                      \
      volatile unsigned long _argvec[3+2];                        \
      volatile unsigned long _res;                                \
      /* _argvec[0] holds current r2 across the call */           \
      _argvec[1]   = (unsigned long)_orig.r2;                     \
      _argvec[2]   = (unsigned long)_orig.nraddr;                 \
      _argvec[2+1] = (unsigned long)arg1;                         \
      _argvec[2+2] = (unsigned long)arg2;                         \
      __asm__ volatile(                                           \
         "mr 11,%1\n\t"                                           \
         "std 2,-16(11)\n\t"  /* save tocptr */                   \
         "ld   2,-8(11)\n\t"  /* use nraddr's tocptr */           \
         "ld   3, 8(11)\n\t"  /* arg1->r3 */                      \
         "ld   4, 16(11)\n\t" /* arg1->r4 */                      \
         "ld  11, 0(11)\n\t"  /* target->r11 */                   \
         VALGRIND_BRANCH_AND_LINK_TO_NOREDIR_R11                  \
         "mr 11,%1\n\t"                                           \
         "mr %0,3\n\t"                                            \
         "ld 2,-16(11)" /* restore tocptr */                      \
         : /*out*/   "=r" (_res)                                  \
         : /*in*/    "r" (&_argvec[2])                            \
         : /*trash*/ "cc", "memory", __CALLER_SAVED_REGS          \
      );                                                          \
      lval = (__typeof__(lval)) _res;                             \
   } while (0)

#endif /* ARCH_ppc64 */


/* ------------------------------------------------------------------ */
/* ARCHITECTURE INDEPENDENT MACROS for CLIENT REQUESTS.               */
/*                                                                    */
/* ------------------------------------------------------------------ */

/* Some request codes.  There are many more of these, but most are not
   exposed to end-user view.  These are the public ones, all of the
   form 0x1000 + small_number.

   Core ones are in the range 0x00000000--0x0000ffff.  The non-public
   ones start at 0x2000.
*/

/* These macros are used by tools -- they must be public, but don't
   embed them into other programs. */
#define VG_USERREQ_TOOL_BASE(a,b) \
   ((unsigned int)(((a)&0xff) << 24 | ((b)&0xff) << 16))
#define VG_IS_TOOL_USERREQ(a, b, v) \
   (VG_USERREQ_TOOL_BASE(a,b) == ((v) & 0xffff0000))

typedef
   enum { VG_USERREQ__RUNNING_ON_VALGRIND  = 0x1001,
          VG_USERREQ__DISCARD_TRANSLATIONS = 0x1002,

          /* These allow any function to be called from the simulated
             CPU but run on the real CPU.  Nb: the first arg passed to
             the function is always the ThreadId of the running
             thread!  So CLIENT_CALL0 actually requires a 1 arg
             function, etc. */
          VG_USERREQ__CLIENT_CALL0 = 0x1101,
          VG_USERREQ__CLIENT_CALL1 = 0x1102,
          VG_USERREQ__CLIENT_CALL2 = 0x1103,
          VG_USERREQ__CLIENT_CALL3 = 0x1104,

          /* Can be useful in regression testing suites -- eg. can
             send Valgrind's output to /dev/null and still count
             errors. */
          VG_USERREQ__COUNT_ERRORS = 0x1201,

          /* These are useful and can be interpreted by any tool that
             tracks malloc() et al, by using vg_replace_malloc.c. */
          VG_USERREQ__MALLOCLIKE_BLOCK = 0x1301,
          VG_USERREQ__FREELIKE_BLOCK   = 0x1302,
          /* Memory pool support. */
          VG_USERREQ__CREATE_MEMPOOL   = 0x1303,
          VG_USERREQ__DESTROY_MEMPOOL  = 0x1304,
          VG_USERREQ__MEMPOOL_ALLOC    = 0x1305,
          VG_USERREQ__MEMPOOL_FREE     = 0x1306,

          /* Allow printfs to valgrind log. */
          VG_USERREQ__PRINTF           = 0x1401,
          VG_USERREQ__PRINTF_BACKTRACE = 0x1402,

          /* Stack support. */
          VG_USERREQ__STACK_REGISTER   = 0x1501,
          VG_USERREQ__STACK_DEREGISTER = 0x1502,
          VG_USERREQ__STACK_CHANGE     = 0x1503,
   } Vg_ClientRequest;

#if !defined(__GNUC__)
#  define __extension__ /* */
#endif

/* Returns the number of Valgrinds this code is running under.  That
   is, 0 if running natively, 1 if running under Valgrind, 2 if
   running under Valgrind which is running under another Valgrind,
   etc. */
#define RUNNING_ON_VALGRIND  __extension__                        \
   ({unsigned int _qzz_res;                                       \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 /* if not */,          \
                               VG_USERREQ__RUNNING_ON_VALGRIND,   \
                               0, 0, 0, 0);                       \
    _qzz_res;                                                     \
   })


/* Discard translation of code in the range [_qzz_addr .. _qzz_addr +
   _qzz_len - 1].  Useful if you are debugging a JITter or some such,
   since it provides a way to make sure valgrind will retranslate the
   invalidated area.  Returns no value. */
#define VALGRIND_DISCARD_TRANSLATIONS(_qzz_addr,_qzz_len)         \
   {unsigned int _qzz_res;                                        \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0,                       \
                               VG_USERREQ__DISCARD_TRANSLATIONS,  \
                               _qzz_addr, _qzz_len, 0, 0);        \
   }


/* These requests are for getting Valgrind itself to print something.
   Possibly with a backtrace.  This is a really ugly hack. */

#if defined(NVALGRIND)

#  define VALGRIND_PRINTF(...)
#  define VALGRIND_PRINTF_BACKTRACE(...)

#else /* NVALGRIND */

int VALGRIND_PRINTF(const char *format, ...)
   __attribute__((format(__printf__, 1, 2)));
__attribute__((weak))
int
VALGRIND_PRINTF(const char *format, ...)
{
   unsigned long _qzz_res;
   va_list vargs;
   va_start(vargs, format);
   VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0, VG_USERREQ__PRINTF,
                           (unsigned long)format, (unsigned long)vargs, 0, 0);
   va_end(vargs);
   return (int)_qzz_res;
}

int VALGRIND_PRINTF_BACKTRACE(const char *format, ...)
   __attribute__((format(__printf__, 1, 2)));
__attribute__((weak))
int
VALGRIND_PRINTF_BACKTRACE(const char *format, ...)
{
   unsigned long _qzz_res;
   va_list vargs;
   va_start(vargs, format);
   VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0, VG_USERREQ__PRINTF_BACKTRACE,
                           (unsigned long)format, (unsigned long)vargs, 0, 0);
   va_end(vargs);
   return (int)_qzz_res;
}

#endif /* NVALGRIND */


/* These requests allow control to move from the simulated CPU to the
   real CPU, calling an arbitary function */
#define VALGRIND_NON_SIMD_CALL0(_qyy_fn)                          \
   ({unsigned long _qyy_res;                                      \
    VALGRIND_DO_CLIENT_REQUEST(_qyy_res, 0 /* default return */,  \
                               VG_USERREQ__CLIENT_CALL0,          \
                               _qyy_fn,                           \
                               0, 0, 0);                          \
    _qyy_res;                                                     \
   })

#define VALGRIND_NON_SIMD_CALL1(_qyy_fn, _qyy_arg1)               \
   ({unsigned long _qyy_res;                                      \
    VALGRIND_DO_CLIENT_REQUEST(_qyy_res, 0 /* default return */,  \
                               VG_USERREQ__CLIENT_CALL1,          \
                               _qyy_fn,                           \
                               _qyy_arg1, 0, 0);                  \
    _qyy_res;                                                     \
   })

#define VALGRIND_NON_SIMD_CALL2(_qyy_fn, _qyy_arg1, _qyy_arg2)    \
   ({unsigned long _qyy_res;                                      \
    VALGRIND_DO_CLIENT_REQUEST(_qyy_res, 0 /* default return */,  \
                               VG_USERREQ__CLIENT_CALL2,          \
                               _qyy_fn,                           \
                               _qyy_arg1, _qyy_arg2, 0);          \
    _qyy_res;                                                     \
   })

#define VALGRIND_NON_SIMD_CALL3(_qyy_fn, _qyy_arg1, _qyy_arg2, _qyy_arg3) \
   ({unsigned long _qyy_res;                                      \
    VALGRIND_DO_CLIENT_REQUEST(_qyy_res, 0 /* default return */,  \
                               VG_USERREQ__CLIENT_CALL3,          \
                               _qyy_fn,                           \
                               _qyy_arg1, _qyy_arg2, _qyy_arg3);  \
    _qyy_res;                                                     \
   })


/* Counts the number of errors that have been recorded by a tool.  Nb:
   the tool must record the errors with VG_(maybe_record_error)() or
   VG_(unique_error)() for them to be counted. */
#define VALGRIND_COUNT_ERRORS                                     \
   ({unsigned int _qyy_res;                                       \
    VALGRIND_DO_CLIENT_REQUEST(_qyy_res, 0 /* default return */,  \
                               VG_USERREQ__COUNT_ERRORS,          \
                               0, 0, 0, 0);                       \
    _qyy_res;                                                     \
   })

/* Mark a block of memory as having been allocated by a malloc()-like
   function.  `addr' is the start of the usable block (ie. after any
   redzone) `rzB' is redzone size if the allocator can apply redzones;
   use '0' if not.  Adding redzones makes it more likely Valgrind will spot
   block overruns.  `is_zeroed' indicates if the memory is zeroed, as it is
   for calloc().  Put it immediately after the point where a block is
   allocated. 
   
   If you're allocating memory via superblocks, and then handing out small
   chunks of each superblock, if you don't have redzones on your small
   blocks, it's worth marking the superblock with VALGRIND_MAKE_NOACCESS
   when it's created, so that block overruns are detected.  But if you can
   put redzones on, it's probably better to not do this, so that messages
   for small overruns are described in terms of the small block rather than
   the superblock (but if you have a big overrun that skips over a redzone,
   you could miss an error this way).  See memcheck/tests/custom_alloc.c
   for an example.

   Nb: block must be freed via a free()-like function specified
   with VALGRIND_FREELIKE_BLOCK or mismatch errors will occur. */
#define VALGRIND_MALLOCLIKE_BLOCK(addr, sizeB, rzB, is_zeroed)    \
   {unsigned int _qzz_res;                                        \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0,                       \
                               VG_USERREQ__MALLOCLIKE_BLOCK,      \
                               addr, sizeB, rzB, is_zeroed);      \
   }

/* Mark a block of memory as having been freed by a free()-like function.
   `rzB' is redzone size;  it must match that given to
   VALGRIND_MALLOCLIKE_BLOCK.  Memory not freed will be detected by the leak
   checker.  Put it immediately after the point where the block is freed. */
#define VALGRIND_FREELIKE_BLOCK(addr, rzB)                        \
   {unsigned int _qzz_res;                                        \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0,                       \
                               VG_USERREQ__FREELIKE_BLOCK,        \
                               addr, rzB, 0, 0);                  \
   }

/* Create a memory pool. */
#define VALGRIND_CREATE_MEMPOOL(pool, rzB, is_zeroed)             \
   {unsigned int _qzz_res;                                        \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0,                       \
                               VG_USERREQ__CREATE_MEMPOOL,        \
                               pool, rzB, is_zeroed, 0);          \
   }

/* Destroy a memory pool. */
#define VALGRIND_DESTROY_MEMPOOL(pool)                            \
   {unsigned int _qzz_res;                                        \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0,                       \
                               VG_USERREQ__DESTROY_MEMPOOL,       \
                               pool, 0, 0, 0);                    \
   }

/* Associate a piece of memory with a memory pool. */
#define VALGRIND_MEMPOOL_ALLOC(pool, addr, size)                  \
   {unsigned int _qzz_res;                                        \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0,                       \
                               VG_USERREQ__MEMPOOL_ALLOC,         \
                               pool, addr, size, 0);              \
   }

/* Disassociate a piece of memory from a memory pool. */
#define VALGRIND_MEMPOOL_FREE(pool, addr)                         \
   {unsigned int _qzz_res;                                        \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0,                       \
                               VG_USERREQ__MEMPOOL_FREE,          \
                               pool, addr, 0, 0);                 \
   }

/* Mark a piece of memory as being a stack. Returns a stack id. */
#define VALGRIND_STACK_REGISTER(start, end)                       \
   ({unsigned int _qzz_res;                                       \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0,                       \
                               VG_USERREQ__STACK_REGISTER,        \
                               start, end, 0, 0);                 \
    _qzz_res;                                                     \
   })

/* Unmark the piece of memory associated with a stack id as being a
   stack. */
#define VALGRIND_STACK_DEREGISTER(id)                             \
   {unsigned int _qzz_res;                                        \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0,                       \
                               VG_USERREQ__STACK_DEREGISTER,      \
                               id, 0, 0, 0);                      \
   }

/* Change the start and end address of the stack id. */
#define VALGRIND_STACK_CHANGE(id, start, end)                     \
   {unsigned int _qzz_res;                                        \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0,                       \
                               VG_USERREQ__STACK_CHANGE,          \
                               id, start, end, 0);                \
   }


#undef ARCH_x86
#undef ARCH_amd64
#undef ARCH_ppc32
#undef ARCH_ppc64

#endif   /* __VALGRIND_H */
