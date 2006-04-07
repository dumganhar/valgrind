
/*--------------------------------------------------------------------*/
/*--- An example Valgrind tool.                          lk_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Lackey, an example Valgrind tool that does
   some simple program measurement and tracing.

   Copyright (C) 2002-2005 Nicholas Nethercote
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

// This tool shows how to do some basic instrumentation.
//
// There are three kinds of instrumentation it can do.  They can be turned
// on/off independently with command line options:
//
// * --basic-counts   : do basic counts, eg. number of instructions
//                      executed, jumps executed, etc.
// * --detailed-counts: do more detailed counts:  number of loads, stores
//                      and ALU operations of different sizes.
// * --trace-mem=yes:   trace all (data) memory accesses.
//
// The code for each kind of instrumentation is guarded by a clo_* variable:
// clo_basic_counts, clo_detailed_counts and clo_trace_mem.
//
// If you want to modify any of the instrumentation code, look for the code
// that is guarded by the relevant clo_* variable (eg. clo_trace_mem)
// If you're not interested in the other kinds of instrumentation you can
// remove them.  If you want to do more complex modifications, please read
// VEX/pub/libvex_ir.h to understand the intermediate representation.
//
//
// Specific Details about --trace-mem=yes
// --------------------------------------
// The address trace produced by --trace-mem=yes is good, but not perfect;
// see Section 3.3.7 of Nicholas Nethercote's PhD dissertation "Dynamic
// Binary Analysis and Instrumentation", 2004, for details about the few
// loads and stores that it misses, and other caveats about the accuracy of
// the address trace.
//
// [Actually, the traces aren't quite right because instructions that modify
// a memory location are treated like a load followed by a store.]
//
// For further inspiration, you should look at cachegrind/cg_main.c which
// handles memory accesses in a more sophisticated way -- it groups them
// together for processing into twos and threes so that fewer C calls are
// made and things run faster.

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_options.h"
#include "pub_tool_machine.h"     // VG_(fnptr_to_fnentry)

/*------------------------------------------------------------*/
/*--- Command line options                                 ---*/
/*------------------------------------------------------------*/

/* Command line options controlling instrumentation kinds, as described at
 * the top of this file. */
static Bool clo_basic_counts    = True;
static Bool clo_detailed_counts = False;
static Bool clo_trace_mem       = False;

/* The name of the function of which the number of calls (under
 * --basic-counts=yes) is to be counted, with default. Override with command
 * line option --fnname. */
static Char* clo_fnname = "_dl_runtime_resolve";

static Bool lk_process_cmd_line_option(Char* arg)
{
   VG_STR_CLO(arg, "--fnname", clo_fnname)
   else VG_BOOL_CLO(arg, "--basic-counts",    clo_basic_counts)
   else VG_BOOL_CLO(arg, "--detailed-counts", clo_detailed_counts)
   else VG_BOOL_CLO(arg, "--trace-mem",       clo_trace_mem)
   else
      return False;
   
   tl_assert(clo_fnname);
   tl_assert(clo_fnname[0]);
   return True;
}

static void lk_print_usage(void)
{  
   VG_(printf)(
"    --basic-counts=no|yes     count instructions, jumps, etc. [no]\n"
"    --detailed-counts=no|yes  count loads, stores and alu ops [no]\n"
"    --trace-mem=no|yes        trace all loads and stores [no]\n"
"    --fnname=<name>           count calls to <name> (only used if\n"
"                              --basic-count=yes)  [_dl_runtime_resolve]\n"
                           
   );
}

static void lk_print_debug_usage(void)
{  
}

/*------------------------------------------------------------*/
/*--- Data and helpers for --basic-counts                  ---*/
/*------------------------------------------------------------*/

/* Nb: use ULongs because the numbers can get very big */
static ULong n_func_calls    = 0;
static ULong n_BBs_entered   = 0;
static ULong n_BBs_completed = 0;
static ULong n_IRStmts      = 0;
static ULong n_guest_instrs  = 0;
static ULong n_Jccs          = 0;
static ULong n_Jccs_untaken  = 0;

static void add_one_func_call(void)
{
   n_func_calls++;
}

static void add_one_BB_entered(void)
{
   n_BBs_entered++;
}

static void add_one_BB_completed(void)
{
   n_BBs_completed++;
}

static void add_one_IRStmt(void)
{
   n_IRStmts++;
}

static void add_one_guest_instr(void)
{
   n_guest_instrs++;
}

static void add_one_Jcc(void)
{
   n_Jccs++;
}

static void add_one_Jcc_untaken(void)
{
   n_Jccs_untaken++;
}

/*------------------------------------------------------------*/
/*--- Data and helpers for --detailed-counts               ---*/
/*------------------------------------------------------------*/

/* --- Operations --- */

typedef enum { OpLoad=0, OpStore=1, OpAlu=2 } Op;

#define N_OPS 3


/* --- Types --- */

#define N_TYPES 9

static Int type2index ( IRType ty )
{
   switch (ty) {
      case Ity_I1:      return 0;
      case Ity_I8:      return 1;
      case Ity_I16:     return 2;
      case Ity_I32:     return 3;
      case Ity_I64:     return 4;
      case Ity_I128:    return 5;
      case Ity_F32:     return 6;
      case Ity_F64:     return 7;
      case Ity_V128:    return 8;
      default: tl_assert(0); break;
   }
}

static HChar* nameOfTypeIndex ( IRType ty )
{
   switch (ty) {
      case 0: return "I1";   break;
      case 1: return "I8";   break;
      case 2: return "I16";  break;
      case 3: return "I32";  break;
      case 4: return "I64";  break;
      case 5: return "I128"; break;
      case 6: return "F32";  break;
      case 7: return "F64";  break;
      case 8: return "V128"; break;
      default: tl_assert(0); break;
   }
}


/* --- Counts --- */

static ULong detailCounts[N_OPS][N_TYPES];

/* The helper that is called from the instrumented code. */
static VG_REGPARM(1)
void increment_detail(ULong* detail)
{
   (*detail)++;
}

/* A helper that adds the instrumentation for a detail. */
static void instrument_detail(IRBB* bb, Op op, IRType type)
{
   IRDirty* di;
   IRExpr** argv;
   const UInt typeIx = type2index(type);

   tl_assert(op < N_OPS);
   tl_assert(typeIx < N_TYPES);

   argv = mkIRExprVec_1( mkIRExpr_HWord( (HWord)&detailCounts[op][typeIx] ) );
   di = unsafeIRDirty_0_N( 1, "increment_detail",
                              VG_(fnptr_to_fnentry)( &increment_detail ), 
                              argv);
   addStmtToIRBB( bb, IRStmt_Dirty(di) );
}

/* Summarize and print the details. */

static void print_details ( void )
{
   Int typeIx;
   VG_(message)(Vg_UserMsg,
                "   Type        Loads       Stores       AluOps");
   VG_(message)(Vg_UserMsg,
                "   -------------------------------------------");
   for (typeIx = 0; typeIx < N_TYPES; typeIx++) {
      VG_(message)(Vg_UserMsg,
                   "   %4s %,12llu %,12llu %,12llu", 
                   nameOfTypeIndex( typeIx ),
                   detailCounts[OpLoad ][typeIx],
                   detailCounts[OpStore][typeIx],
                   detailCounts[OpAlu  ][typeIx]
      );
   }
}


/*------------------------------------------------------------*/
/*--- Data and helpers for --trace-mem                     ---*/
/*------------------------------------------------------------*/

static VG_REGPARM(2) void trace_load(Addr addr, SizeT size)
{
   VG_(printf)("load : %p, %d\n", addr, size);
}

static VG_REGPARM(2) void trace_store(Addr addr, SizeT size)
{
   VG_(printf)("store: %p, %d\n", addr, size);
}


/*------------------------------------------------------------*/
/*--- Basic tool functions                                 ---*/
/*------------------------------------------------------------*/

static void lk_post_clo_init(void)
{
   Int op, tyIx;

   if (clo_detailed_counts) {
      for (op = 0; op < N_OPS; op++)
         for (tyIx = 0; tyIx < N_TYPES; tyIx++)
            detailCounts[op][tyIx] = 0;
   }
}

static
IRBB* lk_instrument ( VgCallbackClosure* closure,
                      IRBB* bb_in, 
                      VexGuestLayout* layout, 
                      VexGuestExtents* vge,
                      IRType gWordTy, IRType hWordTy )
{
   IRDirty* di;
   Int      i;
   IRBB*    bb;
   Char     fnname[100];
   IRType   type;
   IRExpr** argv;
   IRExpr*  addr_expr;
   IRExpr*  size_expr;

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   /* Set up BB */
   bb           = emptyIRBB();
   bb->tyenv    = dopyIRTypeEnv(bb_in->tyenv);
   bb->next     = dopyIRExpr(bb_in->next);
   bb->jumpkind = bb_in->jumpkind;

   // Copy verbatim any IR preamble preceding the first IMark
   i = 0;
   while (i < bb_in->stmts_used && bb_in->stmts[i]->tag != Ist_IMark) {
      addStmtToIRBB( bb, bb_in->stmts[i] );
      i++;
   }

   if (clo_basic_counts) {
      /* Count this basic block. */
      di = unsafeIRDirty_0_N( 0, "add_one_BB_entered", 
                                 VG_(fnptr_to_fnentry)( &add_one_BB_entered ),
                                 mkIRExprVec_0() );
      addStmtToIRBB( bb, IRStmt_Dirty(di) );
   }

   for (/*use current i*/; i < bb_in->stmts_used; i++) {
      IRStmt* st = bb_in->stmts[i];
      if (!st || st->tag == Ist_NoOp) continue;

      if (clo_basic_counts) {
         /* Count one VEX statement. */
         di = unsafeIRDirty_0_N( 0, "add_one_IRStmt", 
                                    VG_(fnptr_to_fnentry)( &add_one_IRStmt ), 
                                    mkIRExprVec_0() );
         addStmtToIRBB( bb, IRStmt_Dirty(di) );
      }
      
      switch (st->tag) {
         case Ist_IMark:
            if (clo_basic_counts) {
               /* Count guest instruction. */
               di = unsafeIRDirty_0_N( 0, "add_one_guest_instr",
                                          VG_(fnptr_to_fnentry)( &add_one_guest_instr ), 
                                          mkIRExprVec_0() );
               addStmtToIRBB( bb, IRStmt_Dirty(di) );

               /* An unconditional branch to a known destination in the
                * guest's instructions can be represented, in the IRBB to
                * instrument, by the VEX statements that are the
                * translation of that known destination. This feature is
                * called 'BB chasing' and can be influenced by command
                * line option --vex-guest-chase-thresh.
                *
                * To get an accurate count of the calls to a specific
                * function, taking BB chasing into account, we need to
                * check for each guest instruction (Ist_IMark) if it is
                * the entry point of a function.
                */
               tl_assert(clo_fnname);
               tl_assert(clo_fnname[0]);
               if (VG_(get_fnname_if_entry)(st->Ist.IMark.addr, 
                                            fnname, sizeof(fnname))
                   && 0 == VG_(strcmp)(fnname, clo_fnname)) {
                  di = unsafeIRDirty_0_N( 
                          0, "add_one_func_call", 
                             VG_(fnptr_to_fnentry)( &add_one_func_call ), 
                             mkIRExprVec_0() );
                  addStmtToIRBB( bb, IRStmt_Dirty(di) );
               }
            }
            addStmtToIRBB( bb, st );
            break;

         case Ist_Exit:
            if (clo_basic_counts) {
               /* Count Jcc */
               di = unsafeIRDirty_0_N( 0, "add_one_Jcc", 
                                          VG_(fnptr_to_fnentry)( &add_one_Jcc ), 
                                          mkIRExprVec_0() );
               addStmtToIRBB( bb, IRStmt_Dirty(di) );
            }

            addStmtToIRBB( bb, st );

            if (clo_basic_counts) {
               /* Count non-taken Jcc */
               di = unsafeIRDirty_0_N( 0, "add_one_Jcc_untaken", 
                                          VG_(fnptr_to_fnentry)(
                                             &add_one_Jcc_untaken ),
                                          mkIRExprVec_0() );
               addStmtToIRBB( bb, IRStmt_Dirty(di) );
            }
            break;

         case Ist_Store:
            // Add a call to trace_store() if --trace-mem=yes.
            if (clo_trace_mem) {
               addr_expr = st->Ist.Store.addr;
               size_expr = mkIRExpr_HWord( 
                             sizeofIRType(
                               typeOfIRExpr(bb->tyenv, st->Ist.Store.data)));
               argv = mkIRExprVec_2( addr_expr, size_expr );
               di = unsafeIRDirty_0_N( /*regparms*/2, 
                                       "trace_store",
                                       VG_(fnptr_to_fnentry)( trace_store ), 
                                       argv );
               addStmtToIRBB( bb, IRStmt_Dirty(di) );
            }
            if (clo_detailed_counts) {
               type = typeOfIRExpr(bb->tyenv, st->Ist.Store.data);
               tl_assert(type != Ity_INVALID);
               instrument_detail( bb, OpStore, type );
            }
            addStmtToIRBB( bb, st );
            break;

         case Ist_Tmp:
            // Add a call to trace_load() if --trace-mem=yes.
            if (clo_trace_mem) {
               IRExpr* data = st->Ist.Tmp.data;
               if (data->tag == Iex_Load) {
                  addr_expr = data->Iex.Load.addr;
                  size_expr = mkIRExpr_HWord( sizeofIRType(data->Iex.Load.ty) );
                  argv = mkIRExprVec_2( addr_expr, size_expr );
                  di = unsafeIRDirty_0_N( /*regparms*/2, 
                                          "trace_load",
                                          VG_(fnptr_to_fnentry)( trace_load ), 
                                          argv );
                  addStmtToIRBB( bb, IRStmt_Dirty(di) );
               }
            }
            if (clo_detailed_counts) {
               IRExpr* expr = st->Ist.Tmp.data;
               type = typeOfIRExpr(bb->tyenv, expr);
               tl_assert(type != Ity_INVALID);
               switch (expr->tag) {
                  case Iex_Load:
                     instrument_detail( bb, OpLoad, type );
                     break;
                  case Iex_Unop:
                  case Iex_Binop:
                  case Iex_Triop:
                  case Iex_Qop:
                  case Iex_Mux0X:
                     instrument_detail( bb, OpAlu, type );
                     break;
                  default:
                     break;
               }
            }
            addStmtToIRBB( bb, st );
            break;

         default:
            addStmtToIRBB( bb, st );
      }
   }

   if (clo_basic_counts) {
      /* Count this basic block. */
      di = unsafeIRDirty_0_N( 0, "add_one_BB_completed", 
                                 VG_(fnptr_to_fnentry)( &add_one_BB_completed ),
                                 mkIRExprVec_0() );
      addStmtToIRBB( bb, IRStmt_Dirty(di) );
   }

   return bb;
}

static void lk_fini(Int exitcode)
{
   char percentify_buf[4]; /* Two digits, '%' and 0. */
   const int percentify_size = sizeof(percentify_buf);
   const int percentify_decs = 0;
   
   tl_assert(clo_fnname);
   tl_assert(clo_fnname[0]);

   if (clo_basic_counts) {
      VG_(message)(Vg_UserMsg,
         "Counted %,llu calls to %s()", n_func_calls, clo_fnname);

      VG_(message)(Vg_UserMsg, "");
      VG_(message)(Vg_UserMsg, "Jccs:");
      VG_(message)(Vg_UserMsg, "  total:         %,llu", n_Jccs);
      VG_(percentify)((n_Jccs - n_Jccs_untaken), (n_Jccs ? n_Jccs : 1),
         percentify_decs, percentify_size, percentify_buf);
      VG_(message)(Vg_UserMsg, "  taken:         %,llu (%s)", 
         (n_Jccs - n_Jccs_untaken), percentify_buf);
      
      VG_(message)(Vg_UserMsg, "");
      VG_(message)(Vg_UserMsg, "Executed:");
      VG_(message)(Vg_UserMsg, "  BBs entered:   %,llu", n_BBs_entered);
      VG_(message)(Vg_UserMsg, "  BBs completed: %,llu", n_BBs_completed);
      VG_(message)(Vg_UserMsg, "  guest instrs:  %,llu", n_guest_instrs);
      VG_(message)(Vg_UserMsg, "  IRStmts:       %,llu", n_IRStmts);
      
      VG_(message)(Vg_UserMsg, "");
      VG_(message)(Vg_UserMsg, "Ratios:");
      tl_assert(n_BBs_entered); // Paranoia time.
      VG_(message)(Vg_UserMsg, "  guest instrs : BB entered  = %3u : 10",
         10 * n_guest_instrs / n_BBs_entered);
      VG_(message)(Vg_UserMsg, "       IRStmts : BB entered  = %3u : 10",
         10 * n_IRStmts / n_BBs_entered);
      tl_assert(n_guest_instrs); // Paranoia time.
      VG_(message)(Vg_UserMsg, "       IRStmts : guest instr = %3u : 10",
         10 * n_IRStmts / n_guest_instrs);
   }

   if (clo_detailed_counts) {
      VG_(message)(Vg_UserMsg, "");
      VG_(message)(Vg_UserMsg, "IR-level counts by type:");
      print_details();
   }

   if (clo_basic_counts) {
      VG_(message)(Vg_UserMsg, "");
      VG_(message)(Vg_UserMsg, "Exit code:       %d", exitcode);
   }
}

static void lk_pre_clo_init(void)
{
   VG_(details_name)            ("Lackey");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("an example Valgrind tool");
   VG_(details_copyright_author)(
      "Copyright (C) 2002-2005, and GNU GPL'd, by Nicholas Nethercote.");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);
   VG_(details_avg_translation_sizeB) ( 175 );

   VG_(basic_tool_funcs)          (lk_post_clo_init,
                                   lk_instrument,
                                   lk_fini);
   VG_(needs_command_line_options)(lk_process_cmd_line_option,
                                   lk_print_usage,
                                   lk_print_debug_usage);
}

VG_DETERMINE_INTERFACE_VERSION(lk_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                lk_main.c ---*/
/*--------------------------------------------------------------------*/
