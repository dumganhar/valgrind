
/*--------------------------------------------------------------------*/
/*--- The only header your skin will ever need to #include...      ---*/
/*---                                                    vg_skin.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, an x86 protected-mode emulator 
   designed for debugging and profiling binaries on x86-Unixes.

   Copyright (C) 2000-2002 Julian Seward 
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

   The GNU General Public License is contained in the file LICENSE.
*/

#ifndef __VG_SKIN_H
#define __VG_SKIN_H

#include <stdarg.h>       /* ANSI varargs stuff  */
#include <setjmp.h>       /* for jmp_buf         */

#include "vg_constants_skin.h"


/*====================================================================*/
/*=== Build options and table sizes.                               ===*/
/*====================================================================*/

/* You should be able to change these options or sizes, recompile, and 
   still have a working system. */

/* Total number of integer registers available for allocation.  That's
   all of them except %esp, %edi and %ebp.  %edi is a general spare
   temporary.  %ebp permanently points at VG_(baseBlock).  Note that
   it's important that this tie in with what rankToRealRegNo() says.
   DO NOT CHANGE THIS VALUE FROM 5. !  */
#define VG_MAX_REALREGS 5

/* The maximum number of pthreads that we support.  This is
   deliberately not very high since our implementation of some of the
   scheduler algorithms is surely O(N) in the number of threads, since
   that's simple, at least.  And (in practice) we hope that most
   programs do not need many threads. */
#define VG_N_THREADS 50

/* Maximum number of pthread keys available.  Again, we start low until
   the need for a higher number presents itself. */
#define VG_N_THREAD_KEYS 50


/*====================================================================*/
/*=== Basic types                                                  ===*/
/*====================================================================*/

#define mycat_wrk(aaa,bbb) aaa##bbb
#define mycat(aaa,bbb) mycat_wrk(aaa,bbb)

typedef unsigned char          UChar;
typedef unsigned short         UShort;
typedef unsigned int           UInt;
typedef unsigned long long int ULong;

typedef signed char            Char;
typedef signed short           Short;
typedef signed int             Int;
typedef signed long long int   Long;

typedef unsigned int           Addr;

typedef unsigned char          Bool;
#define False                  ((Bool)0)
#define True                   ((Bool)1)


/* ---------------------------------------------------------------------
   Now the basic types are set up, we can haul in the kernel-interface
   definitions.
   ------------------------------------------------------------------ */

#include "./vg_kerneliface.h"


/*====================================================================*/
/*=== Command-line options                                         ===*/
/*====================================================================*/

/* Verbosity level: 0 = silent, 1 (default), > 1 = more verbose. */
extern Int   VG_(clo_verbosity);

/* Call this if a recognised option was bad for some reason.
   Note: don't use it just because an option was unrecognised -- return 'False'
   from SKN_(process_cmd_line_option) to indicate that. */
extern void VG_(bad_option) ( Char* opt );

/* Client args */
extern Int    VG_(client_argc);
extern Char** VG_(client_argv);

/* Client environment.  Can be inspected with VG_(getenv)() (below) */
extern Char** VG_(client_envp);


/*====================================================================*/
/*=== Printing messages for the user                               ===*/
/*====================================================================*/

/* Print a message prefixed by "xx<pid>xx "; '?' depends on the VgMsgKind.
   Should be used for all user output. */

typedef
   enum { Vg_UserMsg,         /* '?' == '=' */
          Vg_DebugMsg,        /* '?' == '-' */
          Vg_DebugExtraMsg    /* '?' == '+' */
   }
   VgMsgKind;

/* Functions for building a message from multiple parts. */
extern void VG_(start_msg)  ( VgMsgKind kind );
extern void VG_(add_to_msg) ( Char* format, ... );
/* Ends and prints the message.  Appends a newline. */
extern void VG_(end_msg)    ( void );

/* Send a simple, single-part message.  Appends a newline. */
extern void VG_(message)    ( VgMsgKind kind, Char* format, ... );


// SSS: hmm...
#define VGP_PUSHCC(x)
#define VGP_POPCC


/*====================================================================*/
/*=== Useful stuff to call from generated code                     ===*/
/*====================================================================*/

/* ------------------------------------------------------------------ */
/* General stuff */

/* Get the simulated %esp */
extern Addr VG_(get_stack_pointer) ( void );

/* Detect if an address is within Valgrind's stack */
extern Bool VG_(within_stack)(Addr a);

/* Detect if an address is in Valgrind's m_state_static */
extern Bool VG_(within_m_state_static)(Addr a);

/* Check if an address is 4-byte aligned */
#define IS_ALIGNED4_ADDR(aaa_p) (0 == (((UInt)(aaa_p)) & 3))


/* ------------------------------------------------------------------ */
/* Thread-related stuff */

/* Special magic value for an invalid ThreadId.  It corresponds to
   LinuxThreads using zero as the initial value for
   pthread_mutex_t.__m_owner and pthread_cond_t.__c_waiting. */
#define VG_INVALID_THREADID ((ThreadId)(0))

/* ThreadIds are simply indices into the vg_threads[] array. */
typedef 
   UInt 
   ThreadId;

typedef
   struct _ThreadState
   ThreadState;

extern ThreadId     VG_(get_current_tid_1_if_root) ( void );
extern ThreadState* VG_(get_ThreadState)           ( ThreadId tid );

/*====================================================================*/
/*=== Valgrind's version of libc                                   ===*/
/*====================================================================*/

/* Valgrind doesn't use libc at all, for good reasons (trust us).  So here
   are its own versions of C library functions, but with VG_ prefixes.  Note
   that the types of some are slightly different to the real ones.  Some
   extra useful functions are provided too; descriptions of how they work
   are given below. */

#if !defined(NULL)
#  define NULL ((void*)0)
#endif


/* ------------------------------------------------------------------ */
/* stdio.h
 *
 * Note that they all output to the file descriptor given by the
 * --logfile-fd=N argument, which defaults to 2 (stderr).  Hence no
 * need for VG_(fprintf)().  
 *
 * Also note that VG_(printf)() and VG_(vprintf)()
 */
extern void VG_(printf)  ( const char *format, ... );
/* too noisy ...  __attribute__ ((format (printf, 1, 2))) ; */
extern void VG_(sprintf) ( Char* buf, Char *format, ... );
extern void VG_(vprintf) ( void(*send)(Char), 
                           const Char *format, va_list vargs );

/* ------------------------------------------------------------------ */
/* stdlib.h */

extern void* VG_(malloc)         ( Int nbytes );
extern void  VG_(free)           ( void* ptr );
extern void* VG_(calloc)         ( Int nmemb, Int nbytes );
extern void* VG_(realloc)        ( void* ptr, Int size );
extern void* VG_(malloc_aligned) ( Int req_alignB, Int req_pszB );

extern void  VG_(exit)( Int status )
             __attribute__ ((__noreturn__));
/* Print a (panic) message (constant string) appending newline, and abort. */
extern void  VG_(panic) ( Char* str )
             __attribute__ ((__noreturn__));

/* Looks up VG_(client_envp) (above) */
extern Char* VG_(getenv) ( Char* name );

/* Crude stand-in for the glibc system() call. */
extern Int   VG_(system) ( Char* cmd );

extern Long  VG_(atoll)   ( Char* str );

/* Like atoll(), but converts a number of base 2..36 */
extern Long  VG_(atoll36) ( UInt base, Char* str );


/* ------------------------------------------------------------------ */
/* ctype.h functions and related */
extern Bool VG_(isspace) ( Char c );
extern Bool VG_(isdigit) ( Char c );
extern Char VG_(toupper) ( Char c );


/* ------------------------------------------------------------------ */
/* string.h */
extern Int   VG_(strlen)         ( const Char* str );
extern Char* VG_(strcat)         ( Char* dest, const Char* src );
extern Char* VG_(strncat)        ( Char* dest, const Char* src, Int n );
extern Char* VG_(strpbrk)        ( const Char* s, const Char* accept );
extern Char* VG_(strcpy)         ( Char* dest, const Char* src );
extern Char* VG_(strncpy)        ( Char* dest, const Char* src, Int ndest );
extern Int   VG_(strcmp)         ( const Char* s1, const Char* s2 );
extern Int   VG_(strncmp)        ( const Char* s1, const Char* s2, Int nmax );
extern Char* VG_(strstr)         ( const Char* haystack, Char* needle );
extern Char* VG_(strchr)         ( const Char* s, Char c );
extern Char* VG_(strdup)         ( const Char* s);

/* Like strcmp(),  but stops comparing at any whitespace. */
extern Int   VG_(strcmp_ws)      ( const Char* s1, const Char* s2 );

/* Like strncmp(), but stops comparing at any whitespace. */
extern Int   VG_(strncmp_ws)     ( const Char* s1, const Char* s2, Int nmax );

/* Like strncpy(), but if 'src' is longer than 'ndest' inserts a '\0' at the 
   Nth character. */
extern void  VG_(strncpy_safely) ( Char* dest, const Char* src, Int ndest );

/* Mini-regexp function.  Searches for 'pat' in 'str'.  Supports
 * meta-symbols '*' and '?'.  '\' escapes meta-symbols. */
extern Bool  VG_(stringMatch)    ( Char* pat, Char* str );


/* ------------------------------------------------------------------ */
/* math.h */
/* Returns the base-2 logarithm of its argument. */
extern Int VG_(log2) ( Int x );


/* ------------------------------------------------------------------ */
/* unistd.h */
extern Int   VG_(getpid) ( void );


/* ------------------------------------------------------------------ */
/* assert.h */
/* Asserts permanently enabled -- no turning off with NDEBUG.  Hurrah! */
#define VG__STRING(__str)  #__str

#define vg_assert(expr)                                               \
  ((void) ((expr) ? 0 :						      \
	   (VG_(assert_fail) (VG__STRING(expr),			      \
			      __FILE__, __LINE__,                     \
                              __PRETTY_FUNCTION__), 0)))

extern void VG_(assert_fail) ( Char* expr, Char* file, 
                               Int line, Char* fn )
            __attribute__ ((__noreturn__));


/* ------------------------------------------------------------------ */
/* Reading and writing files. */
// SSS: this is all a bit shabby... could the flags be specified in
//      vg_kerneliface.h and revert to normal open()?
/* Equivalent to open(pathname, O_RDONLY). */
extern Int  VG_(open_read) ( Char* pathname );

/* Equivalent to open(pathname, O_WRONLY | O_TRUNC). */
extern Int  VG_(open_write)       ( Char* pathname );

/* Equivalent to open(pathname, O_WRONLY | O_CREAT). */
extern Int  VG_(create_and_write) ( Char* pathname );

/* As per the system calls */
extern Int  VG_(read)      ( Int fd, void* buf, Int count);
extern Int  VG_(write)     ( Int fd, void* buf, Int count);
extern void VG_(close)     ( Int fd );

extern Int  VG_(stat) ( Char* file_name, struct vki_stat* buf );


/* ------------------------------------------------------------------ */
/* mmap and related functions ... */
extern void* VG_(mmap)( void* start, UInt length, 
                        UInt prot, UInt flags, UInt fd, UInt offset );
extern Int  VG_(munmap)( void* start, Int length );

/* Get memory by anonymous mmap. */
extern void* VG_(get_memory_from_mmap) ( Int nBytes, Char* who );


/* ------------------------------------------------------------------ */
/* signal.h.  
  
   Note that these use the vk_ (kernel) structure
   definitions, which are different in places from those that glibc
   defines -- hence the 'k' prefix.  Since we're operating right at the
   kernel interface, glibc's view of the world is entirely irrelevant. */

/* --- Signal set ops --- */
extern Int  VG_(ksigfillset)( vki_ksigset_t* set );
extern Int  VG_(ksigemptyset)( vki_ksigset_t* set );

extern Bool VG_(kisfullsigset)( vki_ksigset_t* set );
extern Bool VG_(kisemptysigset)( vki_ksigset_t* set );

extern Int  VG_(ksigaddset)( vki_ksigset_t* set, Int signum );
extern Int  VG_(ksigdelset)( vki_ksigset_t* set, Int signum );
extern Int  VG_(ksigismember) ( vki_ksigset_t* set, Int signum );

extern void VG_(ksigaddset_from_set)( vki_ksigset_t* dst, 
                                      vki_ksigset_t* src );
extern void VG_(ksigdelset_from_set)( vki_ksigset_t* dst, 
                                      vki_ksigset_t* src );

/* --- Mess with the kernel's sig state --- */
extern Int VG_(ksigprocmask)( Int how, const vki_ksigset_t* set, 
                                       vki_ksigset_t* oldset );
extern Int VG_(ksigaction) ( Int signum,  
                             const vki_ksigaction* act,  
                             vki_ksigaction* oldact );

extern Int VG_(ksignal)(Int signum, void (*sighandler)(Int));

extern Int VG_(ksigaltstack)( const vki_kstack_t* ss, vki_kstack_t* oss );

extern Int VG_(kill)( Int pid, Int signo );
extern Int VG_(sigpending) ( vki_ksigset_t* set );


/*====================================================================*/
/*=== UCode definition                                             ===*/
/*====================================================================*/

/* Tags which describe what operands are. */
typedef
   enum { TempReg=0, ArchReg=1, RealReg=2, 
          SpillNo=3, Literal=4, Lit16=5, 
          NoValue=6 }
   Tag;

/* An invalid temporary number :-) */
#define INVALID_TEMPREG 999999999

/* Microinstruction opcodes. */
typedef
   enum {
      NOP,
      GET,
      PUT,
      LOAD,
      STORE,
      MOV,
      CMOV, /* Used for cmpxchg and cmov */
      WIDEN,
      JMP,

      /* Read/write the %EFLAGS register into a TempReg. */
      GETF, PUTF,

      ADD, ADC, AND, OR,  XOR, SUB, SBB,
      SHL, SHR, SAR, ROL, ROR, RCL, RCR,
      NOT, NEG, INC, DEC, BSWAP,
      CC2VAL,

      /* Not strictly needed, but useful for making better
         translations of address calculations. */
      LEA1,  /* reg2 := const + reg1 */
      LEA2,  /* reg3 := const + reg1 + reg2 * 1,2,4 or 8 */

      /* not for translating x86 calls -- only to call helpers */
      CALLM_S, CALLM_E, /* Mark start and end of push/pop sequences
                           for CALLM. */
      PUSH, POP, CLEAR, /* Add/remove/zap args for helpers. */
      CALLM,  /* call to a machine-code helper */

      /* for calling C functions -- CCALL_M_N passes M arguments and returns N
       * (0 or 1) return values */
      CCALL_0_0, CCALL_1_0, CCALL_2_0,

      /* Hack for translating string (REP-) insns.  Jump to literal if
         TempReg/RealReg is zero. */
      JIFZ,

      /* FPU ops which read/write mem or don't touch mem at all. */
      FPU_R,
      FPU_W,
      FPU,

      /* Advance the simulated %eip by some small (< 128) number. */
      INCEIP,

      /* Makes it easy for extended-UCode ops by doing:

           enum { EU_OP1 = DUMMY_FINAL_OP + 1, ... } 
   
         WARNING: Do not add new opcodes after this one!  They can be added
         before, though. */
      DUMMY_FINAL_UOPCODE
   }
   Opcode;


/* Condition codes, observing the Intel encoding.  CondAlways is an
   extra. */
typedef
   enum {
      CondO      = 0,  /* overflow           */
      CondNO     = 1,  /* no overflow        */
      CondB      = 2,  /* below              */
      CondNB     = 3,  /* not below          */
      CondZ      = 4,  /* zero               */
      CondNZ     = 5,  /* not zero           */
      CondBE     = 6,  /* below or equal     */
      CondNBE    = 7,  /* not below or equal */
      CondS      = 8,  /* negative           */
      ConsNS     = 9,  /* not negative       */
      CondP      = 10, /* parity even        */
      CondNP     = 11, /* not parity even    */
      CondL      = 12, /* jump less          */
      CondNL     = 13, /* not less           */
      CondLE     = 14, /* less or equal      */
      CondNLE    = 15, /* not less or equal  */
      CondAlways = 16  /* Jump always        */
   } 
   Condcode;


/* Descriptions of additional properties of *unconditional* jumps. */
typedef
   enum {
     JmpBoring=0,   /* boring unconditional jump */
     JmpCall=1,     /* jump due to an x86 call insn */
     JmpRet=2,      /* jump due to an x86 ret insn */
     JmpSyscall=3,  /* do a system call, then jump */
     JmpClientReq=4 /* do a client request, then jump */
   }
   JmpKind;


/* Flags.  User-level code can only read/write O(verflow), S(ign),
   Z(ero), A(ux-carry), C(arry), P(arity), and may also write
   D(irection).  That's a total of 7 flags.  A FlagSet is a bitset,
   thusly: 
      76543210
       DOSZACP
   and bit 7 must always be zero since it is unused.
*/
typedef UChar FlagSet;

#define FlagD (1<<6)
#define FlagO (1<<5)
#define FlagS (1<<4)
#define FlagZ (1<<3)
#define FlagA (1<<2)
#define FlagC (1<<1)
#define FlagP (1<<0)

#define FlagsOSZACP (FlagO | FlagS | FlagZ | FlagA | FlagC | FlagP)
#define FlagsOSZAP  (FlagO | FlagS | FlagZ | FlagA |         FlagP)
#define FlagsOSZCP  (FlagO | FlagS | FlagZ |         FlagC | FlagP)
#define FlagsOSACP  (FlagO | FlagS |         FlagA | FlagC | FlagP)
#define FlagsSZACP  (        FlagS | FlagZ | FlagA | FlagC | FlagP)
#define FlagsSZAP   (        FlagS | FlagZ | FlagA |         FlagP)
#define FlagsZCP    (                FlagZ         | FlagC | FlagP)
#define FlagsOC     (FlagO |                         FlagC        )
#define FlagsAC     (                        FlagA | FlagC        )

#define FlagsALL    (FlagsOSZACP | FlagD)
#define FlagsEmpty  (FlagSet)0


/* A Micro (u)-instruction. */
typedef
   struct {
      /* word 1 */
      UInt    lit32;      /* 32-bit literal */

      /* word 2 */
      UShort  val1;       /* first operand */
      UShort  val2;       /* second operand */

      /* word 3 */
      UShort  val3;       /* third operand */
      UChar   opcode;     /* opcode */
      UChar   size;       /* data transfer size */

      /* word 4 */
      FlagSet flags_r;    /* :: FlagSet */
      FlagSet flags_w;    /* :: FlagSet */
      UChar   tag1:4;     /* first  operand tag */
      UChar   tag2:4;     /* second operand tag */
      UChar   tag3:4;     /* third  operand tag */
      UChar   extra4b:4;  /* Spare field, used by WIDEN for src
                             -size, and by LEA2 for scale (1,2,4 or 8),
                             and by JMPs for original x86 instr size */

      /* word 5 */
      UChar   cond;            /* condition, for jumps */
      Bool    smc_check:1;     /* do a smc test, if writes memory. */
      Bool    signed_widen:1;  /* signed or unsigned WIDEN ? */
      JmpKind jmpkind:3;       /* additional properties of unconditional JMP */
   }
   UInstr;


/* Expandable arrays of uinstrs. */
typedef 
   struct { 
      Int     used; 
      Int     size; 
      UInstr* instrs;
      Int     nextTemp;
   }
   UCodeBlock;


/*====================================================================*/
/*=== Instrumenting UCode                                          ===*/
/*====================================================================*/

/* A structure for communicating temp uses (and for indicating
   temp->real register mappings for patchUInstr in core). */
typedef
   struct {
      Int   realNo;
      Int   tempNo;
      Bool  isWrite;
   }
   TempUse;

/* Find what this instruction does to the temps.  Useful for
   optimisation/analysis passes */
extern Int   VG_(getTempUsage) ( UInstr* u, TempUse* arr );


/* ------------------------------------------------------------------ */
/* Used to register helper functions to be called from generated code */
extern void VG_(register_compact_helper)    ( Addr a );
extern void VG_(register_noncompact_helper) ( Addr a );


/* ------------------------------------------------------------------ */
/* Virtual register allocation */

/* Get a new virtual register */
extern Int   VG_(getNewTemp)     ( UCodeBlock* cb );

/* Get a new virtual shadow register */
extern Int   VG_(getNewShadow)   ( UCodeBlock* cb );

/* Get a virtual register's corresponding virtual shadow register */
#define SHADOW(tempreg)  ((tempreg)+1)


/* ------------------------------------------------------------------ */
/* Low-level UInstr builders */
extern void  VG_(emptyUInstr)( UInstr* u );
extern void  VG_(newUInstr0) ( UCodeBlock* cb, Opcode opcode, Int sz );
extern void  VG_(newUInstr1) ( UCodeBlock* cb, Opcode opcode, Int sz,
                               Tag tag1, UInt val1 );
extern void  VG_(newUInstr2) ( UCodeBlock* cb, Opcode opcode, Int sz,
                               Tag tag1, UInt val1,
                               Tag tag2, UInt val2 );
extern void  VG_(newUInstr3) ( UCodeBlock* cb, Opcode opcode, Int sz,
                               Tag tag1, UInt val1,
                               Tag tag2, UInt val2,
                               Tag tag3, UInt val3 );
extern void  VG_(setFlagRW)  ( UInstr* u, 
                               FlagSet fr, FlagSet fw );
extern void  VG_(setLiteralField) ( UCodeBlock* cb, UInt lit32 );

extern void  VG_(copyUInstr)  ( UCodeBlock* cb, UInstr* instr );

extern Bool VG_(anyFlagUse) ( UInstr* u );

/* Refer to `the last instruction stuffed in' (can be lvalue). */
#define LAST_UINSTR(cb) (cb)->instrs[(cb)->used-1]


/* ------------------------------------------------------------------ */
/* Higher-level UInstr sequence builders */
extern void VG_(callHelper_0_0)(UCodeBlock* cb, Addr f);
extern void VG_(callHelper_1_0)(UCodeBlock* cb, Addr f, UInt arg1);
extern void VG_(callHelper_2_0)(UCodeBlock* cb, Addr f, UInt arg1, UInt arg2);


/* ------------------------------------------------------------------ */
/* UCode pretty printing, to help debugging skins;  but only useful
   if VG_(needs).extends_UCode == True. */

/* When True, all generated code is/should be printed. */
extern Bool  VG_(print_codegen);

extern void  VG_(ppUCodeBlock)    ( UCodeBlock* cb, Char* title );
extern void  VG_(ppUInstr)        ( Int instrNo, UInstr* u );
extern Char* VG_(nameUOpcode)     ( Bool upper, Opcode opc );
extern void  VG_(ppUOperand)      ( UInstr* u, Int operandNo, 
                                    Int sz, Bool parens );


/* ------------------------------------------------------------------ */
/* Allocating/freeing basic blocks of UCode */
extern UCodeBlock* VG_(allocCodeBlock) ( void );
extern void  VG_(freeCodeBlock)        ( UCodeBlock* cb );

// SSS: get rid of this...
extern Int   VG_(rankToRealRegNo) ( Int rank );


/* ------------------------------------------------------------------ */
/* Allocating/freeing small blocks during translation */
// SSS: remove these
extern void* VG_(jitmalloc) ( Int nbytes );
extern void  VG_(jitfree)   ( void* ptr );


/*====================================================================*/
/*=== Functions for generating x86 code from UCode                 ===*/
/*====================================================================*/

/* These are only of interest for skins where 
   VG_(needs).extends_UCode == True. */

/* This is the Intel register encoding. */
// SSS: get rid of this with the LOADV/STOREV push/pop improvement
#define R_EAX 0
#define R_ECX 1
#define R_EDX 2
#define R_EBX 3
#define R_ESP 4
#define R_EBP 5
#define R_ESI 6
#define R_EDI 7

#define R_AL (0+R_EAX)
#define R_CL (0+R_ECX)
#define R_DL (0+R_EDX)
#define R_BL (0+R_EBX)
#define R_AH (4+R_EAX)
#define R_CH (4+R_ECX)
#define R_DH (4+R_EDX)
#define R_BH (4+R_EBX)

/* For pretty printing x86 code */
extern Char* VG_(nameOfIntReg)   ( Int size, Int reg );
extern Char  VG_(nameOfIntSize)  ( Int size );

/* Randomly useful things */
extern UInt  VG_(extend_s_8to32) ( UInt x );

/* Code emitters */
extern void VG_(emitB)  ( UInt b );
extern void VG_(emitW)  ( UInt w );
extern void VG_(emitL)  ( UInt l );
extern void VG_(newEmit)( void );

/* Finding offsets */
extern Int  VG_(helper_offset)     ( Addr a );
extern Int  VG_(shadowRegOffset)   ( Int arch );
extern Int  VG_(shadowFlagsOffset) ( void );

/* Subroutine calls */
void VG_(synth_call_baseBlock_method) ( Bool ensure_shortform, 
                                        Int word_offset );
/* Addressing modes */
void VG_(emit_amode_offregmem_reg) ( Int off, Int regmem, Int reg );
void VG_(emit_amode_ereg_greg)     ( Int e_reg, Int g_reg );

/* v-size (4, or 2 with OSO) insn emitters */
void VG_(emit_movv_offregmem_reg) ( Int sz, Int off, Int areg, Int reg );
void VG_(emit_movv_reg_offregmem) ( Int sz, Int reg, Int off, Int areg );
void VG_(emit_movv_reg_reg)       ( Int sz, Int reg1, Int reg2 );
void VG_(emit_nonshiftopv_lit_reg)( Int sz, Opcode opc, UInt lit, Int reg );
void VG_(emit_shiftopv_lit_reg)   ( Int sz, Opcode opc, UInt lit, Int reg );
void VG_(emit_nonshiftopv_reg_reg)( Int sz, Opcode opc, Int reg1, Int reg2 );
void VG_(emit_movv_lit_reg)       ( Int sz, UInt lit, Int reg );
void VG_(emit_unaryopv_reg)       ( Int sz, Opcode opc, Int reg );
void VG_(emit_pushv_reg)          ( Int sz, Int reg );
void VG_(emit_popv_reg)           ( Int sz, Int reg );

void VG_(emit_pushl_lit32)        ( UInt int32 );
void VG_(emit_pushl_lit8)         ( Int lit8 );
void VG_(emit_cmpl_zero_reg)      ( Int reg );
void VG_(emit_swapl_reg_EAX)      ( Int reg );
void VG_(emit_movv_lit_offregmem) ( Int sz, UInt lit, Int off, Int memreg );

/* b-size (1 byte) instruction emitters */
void VG_(emit_movb_lit_offregmem) ( UInt lit, Int off, Int memreg );
void VG_(emit_movb_reg_offregmem) ( Int reg, Int off, Int areg );
void VG_(emit_unaryopb_reg)       ( Opcode opc, Int reg );
void VG_(emit_testb_lit_reg)      ( UInt lit, Int reg );

/* zero-extended load emitters */
void VG_(emit_movzbl_offregmem_reg) ( Int off, Int regmem, Int reg );
void VG_(emit_movzwl_offregmem_reg) ( Int off, Int areg, Int reg );

/* misc instruction emitters */
void VG_(emit_call_reg)         ( Int reg );
void VG_(emit_add_lit_to_esp)   ( Int lit );
void VG_(emit_jcondshort_delta) ( Condcode cond, Int delta );
void VG_(emit_pushal)           ( void );
void VG_(emit_popal)            ( void );
void VG_(emit_AMD_prefetch_reg) ( Int reg );


/*====================================================================*/
/*=== Execution contexts                                           ===*/
/*====================================================================*/

typedef
   struct _ExeContextRec
   ExeContext;

/* Depth of the stack to consider when comparing contexts, etc. */
typedef 
   enum { LowRes, MedRes, HighRes } 
   ExeContextRes;

/* Compare two ExeContexts, just comparing the top two callers. */
extern Bool VG_(eq_ExeContext) ( ExeContextRes res,
                                 ExeContext* e1, ExeContext* e2 );

/* Print an ExeContext. */
extern void VG_(pp_ExeContext) ( ExeContext* );

/* Take a snapshot of the client's stack.  Search our collection of
   ExeContexts to see if we already have it, and if not, allocate a
   new one.  Either way, return a pointer to the context. */
extern ExeContext* VG_(get_ExeContext) ( ThreadState *tst );


/*====================================================================*/
/*=== Error reporting                                              ===*/
/*====================================================================*/

/* Suppression is a type describing an error which we want to
   suppress, ie, not show the user, usually because it is caused by a
   problem in a library which we can't fix, replace or work around.
   Suppressions are read from a file at startup time, specified by
   vg_clo_suppressions, and placed in the vg_suppressions list.  This
   gives flexibility so that new suppressions can be added to the file
   as and when needed.
*/

typedef
   enum {
      /* Pthreading error */
      PThread,
      FinalDummySuppressionKind
   }
   SuppressionKind;

/* For each caller specified for a suppression, record the nature of
   the caller name. */
   //SSS: when splitting upt two halves of suppression, hide this again
typedef
   enum { 
      /* Name is of an shared object file. */
      ObjName,
      /* Name is of a function. */
      FunName 
   }
   SuppressionLocTy;

/* An extensible (via the 'extra' field) suppression record. */
// SSS: void* extra isn't totally satisfactory... eg. it's a pain
// initialising, copying, etc. the errcontext in two parts...
//
// SSS: can split this up into two halves -- only parts need to be exposed
// to the skin
typedef
   struct _Suppression {
      struct _Suppression* next;
      /* The number of times this error has been suppressed. */
      Int count;
      /* The name by which the suppression is referred to. */
      Char* sname;
      /* What kind of suppression. */
      SuppressionKind skind;
      /* String -- can be used in skin-specific way */
      Char* string;
      /* For any skin-specific extra information */
      void* extra;
      /* Name of fn where err occurs, and immediate caller (mandatory). */
      SuppressionLocTy caller0_ty;
      Char*            caller0;
      SuppressionLocTy caller1_ty;
      Char*            caller1;
      /* Optional extra callers. */
      SuppressionLocTy caller2_ty;
      Char*            caller2;
      SuppressionLocTy caller3_ty;
      Char*            caller3;
   } 
   Suppression;

/* ErrContext is a type for recording just enough info to generate an
   error report.  The idea is that (typically) the same few points in the
   program generate thousands of illegal accesses, and we don't want to
   spew out a fresh error message for each one.  Instead, we use these
   structures to common up duplicates.
*/

/* What kind of error it is. */

typedef
   enum { PThreadErr,
          FinalDummyErrKind
   }
   ErrKind;

/* Top-level struct for recording errors. */
// SSS: split into public and private halves?
typedef
   struct _ErrContext {
      /* ALL */
      struct _ErrContext* next;
      /* ALL */
      /* NULL if unsuppressed; or ptr to suppression record. */
      Suppression* supp;
      /* ALL */
      Int count;
      /* ALL */
      ErrKind ekind;
      /* ALL */
      ExeContext* where;
      /* Used frequently */
      Addr addr;
      /* Used frequently */
      Char* string;
      /* For any skin-specific extras */
      void* extra;
      /* ALL */
      ThreadId tid;
      /* ALL */
      /* These record %EIP, %ESP and %EBP at the error point.  They
         are only used to make GDB-attaching convenient; there is no
         other purpose; specifically they are not used to do
         comparisons between errors. */
      UInt m_eip;
      UInt m_esp;
      UInt m_ebp;
   }
   ErrContext;

/* Returns true if we've seen enough errors that no more should be issued */
extern Bool VG_(ignore_errors) ( void );

// SSS: blah blah blah
extern void VG_(maybe_add_context) ( ErrContext* ec );

// SSS: blah blah blah
extern void VG_(construct_err_context) ( ErrContext* ec, ErrKind ekind, 
                                         Addr a, Char* s, ThreadState* tst );

/* Gets a non-blank, non-comment line of at most nBuf chars from fd.
   Skips leading spaces on the line.  Return Trues if EOF was hit instead. 
*/
extern Bool VG_(getLine) ( Int fd, Char* buf, Int nBuf );


/*====================================================================*/
/*=== Obtaining debug information                                  ===*/
/*====================================================================*/

// SSS: don't necessarily export all these
// SSS: give better names
// SSS: make buf lengths safe
// SSS: add get_debug_info()
extern void VG_(what_obj_and_fun_is_this)
                                          ( Addr a,
                                            Char* obj_buf, Int n_obj_buf,
                                            Char* fun_buf, Int n_fun_buf );
extern Bool VG_(what_line_is_this)        ( Addr a,
                                            UChar* filename, Int n_filename,
                                            UInt* lineno );
extern Bool VG_(what_fn_is_this)          ( Bool no_demangle, Addr a,
                                            Char* fn_name, Int n_fn_name);


/*====================================================================*/
/*=== Shadow chunks and block-finding                              ===*/
/*====================================================================*/

// SSS: not sure about all this...
typedef
   enum { 
      Vg_AllocMalloc = 0,
      Vg_AllocNew    = 1,
      Vg_AllocNewVec = 2 
   }
   VgAllocKind;

// SSS: not sure about this...
/* Description of a malloc'd chunk. */
typedef 
   struct _ShadowChunk {
      struct _ShadowChunk* next;
      ExeContext*   where;          /* where malloc'd/free'd */
      UInt          size : 30;      /* size requested.       */
      VgAllocKind   allockind : 2;  /* which wrapper did the allocation */
      Addr          data;           /* ptr to actual block.  */
   } 
   ShadowChunk;


/* Makes an array of pointers to all the shadow chunks of malloc'd blocks */
extern ShadowChunk** VG_(get_malloc_shadows) ( /*OUT*/ UInt* n_shadows );

/* Determines if address 'a' is within the bounds of the block at start.
   Allows a little 'slop' round the edges. */
extern Bool VG_(addr_is_in_block) ( Addr a, Addr start, UInt size );

/* Searches through currently malloc'd blocks until a matching one is found.
   Returns NULL if none match.

   Extra arguments can be implicitly passed to p using nested functions;
   see vg_memcheck_errcontext.c for an example. */
extern ShadowChunk* VG_(any_matching_mallocd_ShadowChunks) 
                        ( Bool (*p) ( ShadowChunk* ));

/* Searches through recently free'd blocks until a matching one is found.
   Returns NULL if none match.

   Don't call this unless VG_(needs).postpone_mem_reuse == True, or you'll
   get an assertion failure. */
extern ShadowChunk* VG_(any_matching_freed_ShadowChunks) 
                        ( Bool (*p) ( ShadowChunk* ));

/* Searches through all thread's stacks to see if any match.  Returns
 * VG_INVALID_THREADID if none match. */
extern ThreadId VG_(any_matching_thread_stack)
                        ( Bool (*p) ( Addr stack_min, Addr stack_max ));

// SSS: not sure about this...
extern void  VG_(client_malloc_done) ( void );


/*====================================================================*/
/*=== Skin-specific stuff                                          ===*/
/*====================================================================*/

/* Skin-specific settings.
 *
 * If new fields are added to this type, update:
 *  - vg_main.c:VG_(needs) initialisation
 *  - vg_main.c:sanity_check_needs()
 *
 * If the name of this type or any of its fields change, update:
 *  - dependent comments (just search for "VG_(needs)"). 
 */
typedef
   struct {
      /* name and description used in the startup message */
      Char* name;
      Char* description;

      /* Booleans that decide core behaviour */

      /* Need to record exe contexts on malloc'd/free'd/etc blocks? */
      // SSS: these two are pretty gruesome
      Bool record_mem_exe_context;
      /* Postpone dynamic memory use once free'd as long as possible? */
      Bool postpone_mem_reuse;

      /* Debug info needed? */
      Bool debug_info;
      /* Report pthread errors? */
      Bool pthread_errors;
      /* Want to report errors?  This implies includes handling of 
       * suppressions, too. */
      Bool report_errors;

      /* Should __libc_freeres() be run?  Bugs in it crash the skin. */
      Bool run_libc_freeres;

      /* Booleans that indicate extra operations are defined;  if these are
       * True, the corresponding template functions (given below) must be
       * defined.  A lot like being a member of a type class. */

      /* Is information kept about specific individual basic blocks?  (Eg. for
       * cachesim there are cost-centres for every instruction, stored at a
       * basic block level.)  If so, it sometimes has to be discarded, because
       * .so mmap/munmap-ping or self-modifying code (informed by the
       * DISCARD_TRANSLATIONS user request) can cause one instruction address
       * to store information about two different instructions in one program
       * run!
       */
      Bool identifies_basic_blocks;

      /* Maintains information about each register? */
      Bool shadow_regs;

      /* Skin defines its own command line options? */
      Bool command_line_options;
      /* Skin defines its own client requests? */
      Bool client_requests;

      /* Skin defines its own UInstrs? */
      Bool extends_UCode;

      /* Skin does stuff before and/or after system calls? */
      Bool wrap_syscalls;

      /* Are skin-state sanity checks performed? */
      Bool sanity_checks;
   } 
   VgNeeds;

extern VgNeeds VG_(needs);


/* ------------------------------------------------------------------ */
/* Core events to track */

/* Part of the core from which this call was made.  Useful for determining
 * what kind of error message should be emitted. */
typedef 
   enum { Vg_CorePThread, Vg_CoreSignal, Vg_CoreSysCall, Vg_CoreTranslate }
   CorePart;

/* Events happening in core to track.  To be notified, assign a function
 * to the function pointer.  To ignore an event, don't do anything
 * (default assignment is to NULL in which case the call is skipped). */
typedef
   struct {
      /* Memory events */
      void (*new_mem_startup)( Addr a, UInt len, Bool rr, Bool ww, Bool xx );
      void (*new_mem_heap)   ( Addr a, UInt len, Bool is_inited );
      void (*new_mem_stack)  ( Addr a, UInt len );
      void (*new_mem_stack_aligned) ( Addr a, UInt len );
      void (*new_mem_stack_signal)  ( Addr a, UInt len );
      void (*new_mem_brk)    ( Addr a, UInt len );
      void (*new_mem_mmap)   ( Addr a, UInt len, 
                               Bool nn, Bool rr, Bool ww, Bool xx );

      void (*copy_mem_heap)  ( Addr from, Addr to, UInt len );
      void (*change_mem_mprotect) ( Addr a, UInt len,  
                                    Bool nn, Bool rr, Bool ww, Bool xx );
      
      void (*ban_mem_heap)   ( Addr a, UInt len );
      void (*ban_mem_stack)  ( Addr a, UInt len );

      // SSS: alloc_free_kinds_match isn't good.
      void (*die_mem_heap)   ( ThreadState* tst, Addr a, UInt len,
                               Bool alloc_free_kinds_match );
      void (*die_mem_stack) ( Addr a, UInt len );
      void (*die_mem_stack_aligned) ( Addr a, UInt len );
      void (*die_mem_stack_signal)  ( Addr a, UInt len );
      void (*die_mem_brk)    ( Addr a, UInt len );
      void (*die_mem_munmap) ( Addr a, UInt len );

      void (*pre_mem_read)   ( CorePart part, ThreadState* tst,
                               Char* s, Addr a, UInt size );
      void (*pre_mem_read_asciiz) ( CorePart part, ThreadState* tst,
                                    Char* s, Addr a );
      void (*pre_mem_write)  ( CorePart part, ThreadState* tst,
                               Char* s, Addr a, UInt size );
      /* Not implemented yet -- have to add in lots of places, which is a
         pain.  Won't bother unless/until there's a need. */
      /* void (*post_mem_read)  ( ThreadState* tst, Char* s, 
                                  Addr a, UInt size ); */
      void (*post_mem_write) ( Addr a, UInt size );

      /* Mutex events */
      void (*post_mutex_lock)   ( ThreadId tid, 
                                  void* /*pthread_mutex_t* */ mutex );
      void (*post_mutex_unlock) ( ThreadId tid, 
                                  void* /*pthread_mutex_t* */ mutex );
      
      /* Others... threads, condition variables, etc... */

      /* ... */
   }
   VgTrackEvents;

/* Declare the struct instance */
extern VgTrackEvents VG_(track_events);


/* ------------------------------------------------------------------ */
/* Template functions */

/* These are the parameterised functions in the core.  The default definitions
 * are replaced by LD_PRELOADing skin substitutes.  At the very least, a skin
 * must define the fundamental template functions.  Depending on what needs
 * boolean variables are set, extra templates will be used too.  For each
 * group, the need governing its use is mentioned. */


/* ------------------------------------------------------------------ */
/* Fundamental template functions */

// SSS: rename to SK_({pre,post}_init_clo)
extern void        SK_(pre_clo_init) ( VgNeeds* needs, VgTrackEvents* track );
extern void        SK_(post_clo_init)( void );
extern UCodeBlock* SK_(instrument)   ( UCodeBlock* cb, Addr a );
extern void        SK_(fini)         ( void );


/* ------------------------------------------------------------------ */
/* VG_(needs).report_errors */

/* Identify if two errors are equal, or equal enough.  Should not check the
   ExeContext (e->where field) as it's checked separately.  But if
   the ErrContext's extra field contains an ExeContext, 'res' should be
   passed to VG_(eq_ExeContext) if it's looked at.

   (SSS: yuk)
 */
extern Bool SKN_(eq_ErrContext) ( ExeContextRes res,
                                  ErrContext* e1, ErrContext* e2 );

/* Print error context.  It should include a call to
   VG_(pp_ExeContext)(ec->where). */
extern void SKN_(pp_ErrContext) ( ErrContext* ec );

/* Copy the ec->extra part and replace ec->extra with the new copy.  This is
   necessary to move from a temporary stack copy to a permanent one.
  
   Then fill in any details that could be postponed until after the decision
   whether to ignore the error (ie. details not affecting the result of
   SKN_(eq_ErrContext)).  This saves time when errors are ignored.
  
   Yuk.
 */
extern void SKN_(dup_extra_and_update)(ErrContext* ec);

/* Return value indicates recognition.  If recognised, type goes in skind. */
extern Bool SKN_(recognised_suppression) ( Char* name, SuppressionKind *skind );

/* Read any extra info for this suppression kind */
extern Bool SKN_(read_extra_suppression_info) ( Int fd, Char* buf, 
                                                Int nBuf, Suppression *s );

/* This should just check the kinds match and maybe some stuff in the
   'extra' field if appropriate */
extern Bool SKN_(error_matches_suppression)(ErrContext* ec, Suppression* su);


/* ------------------------------------------------------------------ */
/* VG_(needs).identifies_basic_blocks */

extern void SKN_(discard_basic_block_info) ( Addr a, UInt size );


/* ------------------------------------------------------------------ */
/* VG_(needs).shadow_regs */

/* Valid values for general registers and EFLAGS register, for initialising
   and updating registers when written in certain places in core. */
extern void SKN_(written_shadow_regs_values) ( UInt* gen_reg, UInt* eflags );


/* ------------------------------------------------------------------ */
/* VG_(needs).command_line_options */

/* Return True if option was recognised */
extern Bool SKN_(process_cmd_line_option)( UChar* argv );

/* Print out command line usage for skin options */
extern Char* SKN_(usage)                  ( void );


/* ------------------------------------------------------------------ */
/* VG_(needs).client_requests */

extern UInt SKN_(handle_client_request) ( ThreadState* tst, UInt* arg_block );


/* ------------------------------------------------------------------ */
/* VG_(needs).extends_UCode */

extern void  SKN_(emitExtUInstr)  ( UInstr* u );
extern Bool  SKN_(saneExtUInstr)  ( Bool beforeRA, UInstr* u );
extern Char* SKN_(nameExtUOpcode) ( Opcode opc );
extern void  SKN_(ppExtUInstr)    ( UInstr* u );
extern Int   SKN_(getExtTempUsage)( UInstr* u, TempUse* arr );


/* ------------------------------------------------------------------ */
/* VG_(needs).wrap_syscalls */

/* If either of the pre_ functions malloc() something to return, the
 * corresponding post_ function had better free() it! 
 */ 
extern void* SKN_(pre_syscall)  ( ThreadId tid);
extern void  SKN_(post_syscall) ( ThreadId tid, UInt syscallno,
                                  void* pre_result, Int res );

// SSS: this is all messed up and ugly
extern void* SKN_(pre_check_known_blocking_syscall)  
                 ( ThreadId tid, Int syscallno, Int* res );
extern void  SKN_(post_check_known_blocking_syscall) 
                 ( ThreadId tid, Int syscallno, void* pre_result, Int* res );


/* ---------------------------------------------------------------------
   VG_(needs).sanity_checks */

extern Bool SKN_(cheap_sanity_check)     ( void );
extern Bool SKN_(expensive_sanity_check) ( void );


#endif   /* NDEF __VG_SKIN_H */

/*--------------------------------------------------------------------*/
/*--- end                                                vg_skin.h ---*/
/*--------------------------------------------------------------------*/

