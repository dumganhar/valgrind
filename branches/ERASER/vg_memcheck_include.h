/*--------------------------------------------------------------------*/
/*--- A header file for all parts of the MemCheck skin.            ---*/
/*---                                        vg_memcheck_include.h ---*/
/*--------------------------------------------------------------------*/

// UCode extension for efficient memory checking operations
typedef
   enum {
      /* uinstrs which are not needed for mere translation of x86 code,
         only for instrumentation of it. */
      LOADV = DUMMY_FINAL_UOPCODE + 1,
      STOREV,
      GETV,
      PUTV,
      TESTV,
      SETV, 
      /* Get/set the v-bit (and it is only one bit) for the simulated
         %eflags register. */
      GETVF,
      PUTVF,

      /* Do a unary or binary tag op.  Only for post-instrumented
         code.  For TAG1, first and only arg is a TempReg, and is both
         arg and result reg.  For TAG2, first arg is src, second is
         dst, in the normal way; both are TempRegs.  In both cases,
         3rd arg is a RiCHelper with a Lit16 tag.  This indicates
         which tag op to do. */
      TAG1,
      TAG2
   }
   ExtOpcode;

/*------------------------------------------------------------*/
/*--- Skin-specific command line options + defaults        ---*/
/*------------------------------------------------------------*/

/* Allow loads from partially-valid addresses?  default: YES */
Bool VG_(clo_partial_loads_ok);

/* Do leak check at exit?  default: NO */
Bool VG_(clo_leak_check);

/* How closely should we compare ExeContexts in leak records? default: 2 */
Int  VG_(clo_leak_resolution);

/* In leak check, show reachable-but-not-freed blocks?  default: NO */
Bool VG_(clo_show_reachable);

/* Assume accesses immediately below %esp are due to gcc-2.96 bugs.
 * default: NO*/
Bool VG_(clo_workaround_gcc296_bugs);

/* Shall we V-check addrs? (they are always A checked too)   default: YES */
Bool VG_(clo_check_addrVs);

/* DEBUG: clean up instrumented code?  default: YES */
Bool VG_(clo_cleanup);

/*------------------------------------------------------------*/
/*--- Functions                                            ---*/
/*------------------------------------------------------------*/

// Functions defined in vg_memcheck_helpers.S
extern void SK_(helper_value_check4_fail) ( void );
extern void SK_(helper_value_check2_fail) ( void );
extern void SK_(helper_value_check1_fail) ( void );
extern void SK_(helper_value_check0_fail) ( void );

// Functions defined in vg_memcheck.c
extern void SK_(helperc_STOREV4) ( UInt, Addr );
extern void SK_(helperc_STOREV2) ( UInt, Addr );
extern void SK_(helperc_STOREV1) ( UInt, Addr );
   
extern UInt SK_(helperc_LOADV1) ( Addr );
extern UInt SK_(helperc_LOADV2) ( Addr );
extern UInt SK_(helperc_LOADV4) ( Addr );

extern void SK_(fpu_write_check) ( Addr addr, Int size );
extern void SK_(fpu_read_check)  ( Addr addr, Int size );

// Functions defined in vg_memcheck_errcontext.c
extern void SK_(record_value_error)       ( Int size );
extern void SK_(record_address_error)     ( Addr a, Int size, Bool isWrite );
extern void SK_(record_pthread_mem_error) ( ThreadState* tst, Bool isWrite,
                                            Char* s );
extern void SK_(record_param_error)       ( ThreadState* tst, Addr a,   
                                            Bool isWriteLack, Char* msg );
extern void SK_(record_jump_error)        ( ThreadState* tst, Addr a );
extern void SK_(record_free_error)        ( ThreadState* tst, Addr a );
extern void SK_(record_freemismatch_error)( ThreadState* tst, Addr a );
extern void SK_(record_user_error)        ( ThreadState* tst, Addr a, 
                                            Bool isWrite );


/*--------------------------------------------------------------------*/
/*--- end                                    vg_memcheck_include.h ---*/
/*--------------------------------------------------------------------*/

