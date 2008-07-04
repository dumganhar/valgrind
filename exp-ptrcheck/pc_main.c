
/*--------------------------------------------------------------------*/
/*--- Annelid: a pointer-use checker.                    an_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Annelid, a Valgrind skin for checking pointer
   use in programs.

   Copyright (C) 2003 Nicholas Nethercote
      njn25@cam.ac.uk

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

// FIXME: 64-bit cleanness, check the following
// struct _ISNode.ownerCount is 32-bit
// struct _ISNode.topLevel is 32-bit

// FIXME: result of add_new_segment is always ignored

// FIXME: the mechanism involving last_seg_added is really ugly.
// Do something cleaner.

// FIXME: post_reg_write_clientcall: check function pointer comparisons
// are safe on toc-afflicted platforms

// FIXME: tidy up findShadowTmp

// FIXME: looks_like_a_pointer

// XXX: recycle freed segments

//--------------------------------------------------------------
// Metadata:
//   HeapBlock.id :: Seg (stored as heap shadowchunk; always non-zero)
//   MemLoc.aseg  :: Seg (implicitly stored)
//   MemLoc.vseg  :: Seg (explicitly stored as the shadow memory)
//   RegLoc.vseg  :: Seg (explicitly stored as shadow registers)
//
// A Seg is made when new memory is created, eg. with malloc() or mmap().
// There are two other Segs:
//  - NONPTR:  for something that's definitely not a pointer
//  - UNKNOWN: for something that could be a pointer
//  - BOTTOM:  used with pointer differences (see below)
//
// MemLoc.vseg is done at word granularity.  If a pointer is written
// to memory misaligned, the information about it will be lost -- it's
// treated as two sub-word writes to two adjacent words.  This avoids
// certain nasty cases that could arise if we tried to track unaligned
// pointers.  Fortunately, misalignment is rare so we don't lose much
// information this way.
//
// MemLoc.aseg is done at byte granularity, and *implicitly* -- ie. not
// directly accessible like MemLoc.vseg, but only by searching through all
// the segments.  Fortunately, it's mostly checked at LOADs/STOREs;  at that
// point we have a pointer p to the MemLoc m as the other arg of the
// LOAD/STORE, so we can check to see if the p.vseg's range includes m.  If
// not, it's an error and we have to search through all segments to find out
// what m.aseg really is.  That's still pretty fast though, thanks to the
// interval skip-list used.  With syscalls we must also do the skip-list
// search, but only on the first and last bytes touched.
//--------------------------------------------------------------

//--------------------------------------------------------------
// Assumptions, etc:
// - see comment at top of SK_(instrument)() for how sub-word ops are
//   handled.
//
// - ioctl(), socketcall() (and ipc() will be) assumed to return non-pointers
//
// - FPU_W is assumed to never write pointers.
//
// - Assuming none of the post_mem_writes create segments worth tracking.
//
// - Treating mmap'd segments (all! including code) like heap segments.  But
//   their ranges can change, new ones can be created by unmapping parts of
//   old segments, etc.  But this nasty behaviour seems to never happen -- 
//   there are assertions checking it.
//--------------------------------------------------------------

//--------------------------------------------------------------
// What I am checking:
// - Type errors:
//    * ADD, OR, LEA2: error if two pointer inputs.
//    * ADC, SBB: error if one or two pointer inputs.
//    * AND, OR: error if two unequal pointer inputs.
//    * NEG: error if pointer input.
//    * {,i}mul_32_64 if either input is a pointer.
//    * shldl/shrdl, bsf/bsr if any inputs are pointers.
//
// - LOAD, STORE:
//    * ptr.vseg must match ptee.aseg.
//    * ptee.aseg must not be a freed segment.
//
// - syscalls: for those accessing memory, look at first and last bytes:
//    * check first.aseg == last.aseg
//    * check first.aseg and last.aseg are not freed segments.
//
// What I am not checking, that I expected to when I started:
// - AND, XOR: allowing two pointers to be used if both from the same segment,
//   because "xor %r,%r" is commonly used to zero %r, and "test %r,%r"
//   (which is translated with an AND) is common too.
//
// - div_64_32/idiv_64_32 can take pointer inputs for the dividend;
//   division doesn't make sense, but modulo does, and they're done with the
//   same instruction.  (Could try to be super-clever and watch the outputs
//   to see if the quotient is used, but not worth it.)
//
// - mul_64_32/imul_64_32 can take pointers inputs for one arg or the
//   other, but not both.  This is because some programs (eg. Mozilla
//   Firebird) multiply pointers in hash routines.
//
// - NEG: can take a pointer.  It happens in glibc in a few places.  I've
//   seen the code, didn't understand it, but it's done deliberately.
//
// What I am not checking/doing, but could, but it would require more
// instrumentation and/or slow things down a bit:
// - SUB: when differencing two pointers, result is BOTTOM, ie. "don't
//   check".  Could link segments instead, slower but a bit more accurate.
//   Also use BOTTOM when doing (ptr - unknown), which could be a pointer
//   difference with a stack/static pointer.
//
// - PUTF: input should be non-pointer
//
// - arithmetic error messages: eg. for adding two pointers, just giving the
//   segments, not the actual pointers.
//
// What I am not checking, and would be difficult:
// - mmap(...MAP_FIXED...) is not handled specially.  It might be used in
//   ways that fool Annelid into giving false positives.
//
// - syscalls: for those accessing memory, not checking that the asegs of the
//   accessed words match the vseg of the accessing pointer, because the
//   vseg is not easily accessible at the required time (would required
//   knowing for every syscall which register each arg came in, and looking
//   there).
//
// What I am not checking, and would be difficult, but doesn't matter:
// - free(p): similar to syscalls, not checking that the p.vseg matches the
//   aseg of the first byte in the block.  However, Memcheck does an
//   equivalent "bad free" check using shadow_chunks;  indeed, Annelid could
//   do the same check, but there's no point duplicating functionality.  So
//   no loss, really.
//
// Other:
// - not doing anything with mprotect();  probably not worth the effort.
//--------------------------------------------------------------

//--------------------------------------------------------------
// Todo:
// - Segments for stack frames.  Would detect (some, large) stack
//   over/under-runs, dangling pointers.
//
// - Segments for static data.  Would detect over/under-runs.  Requires
//   reading debug info.
//--------------------------------------------------------------

//--------------------------------------------------------------
// Some profiling results:
//                                                 twolf   konq    date sz
// 1. started                                              35.0s   14.7
// 2. introduced GETV/PUTV                                 30.2s   10.1
// 3. inlined check_load_or_store                  5.6s    27.5s   10.1
// 4. (made check_load, check_store4 regparm(0))          (27.9s) (11.0)
// 5. um, not sure                                 5.3s    27.3s   10.6
//    ...
// 6. after big changes, corrections              11.2s    32.8s   14.0
// 7. removed link-segment chasing in check/L/S    8.9s    30.8s   14.0
// 8. avoiding do_lea1 if k is a nonptr            8.0s    28.0s   12.9
//--------------------------------------------------------------

//#include "vg_skin.h"

#include "pub_tool_basics.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_execontext.h"
#include "pub_tool_hashtable.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_replacemalloc.h"
#include "pub_tool_execontext.h"
#include "pub_tool_aspacemgr.h"    // VG_(am_shadow_malloc)
#include "pub_tool_vki.h"          // VKI_MAX_PAGE_SIZE
#include "pub_tool_machine.h"      // VG_({get,set}_shadow_regs_area) et al
#include "pub_tool_debuginfo.h"    // VG_(get_fnname)
#include "pub_tool_threadstate.h"  // VG_(get_running_tid)
#include "pub_tool_oset.h"
#include "pub_tool_vkiscnums.h"

#include "an_list.h"

//#include "vg_profile.c"

#define AN_MALLOC_REDZONE_SZB 0  /* no need for client heap redzones */


//zz /*------------------------------------------------------------*/
//zz /*--- Profiling events                                     ---*/
//zz /*------------------------------------------------------------*/
//zz 
//zz typedef
//zz    enum {
//zz       VgpGetMemAseg = VgpFini+1,
//zz       VgpCheckLoadStore,
//zz       VgpCheckLoad4,
//zz       VgpCheckLoad21,
//zz       VgpCheckStore4,
//zz       VgpCheckStore21,
//zz       VgpCheckFpuR,
//zz       VgpCheckFpuW,
//zz       VgpDoAdd,
//zz       VgpDoSub,
//zz       VgpDoAdcSbb,
//zz       VgpDoXor,
//zz       VgpDoAnd,
//zz       VgpDoOr,
//zz       VgpDoLea1,
//zz       VgpDoLea2,
//zz    }
//zz    VgpSkinCC;

/*------------------------------------------------------------*/
/*--- Command line options                                 ---*/
/*------------------------------------------------------------*/

Bool clo_partial_loads_ok = True;

//zz Bool SK_(process_cmd_line_option)(Char* arg)
//zz {
//zz    if      (VG_CLO_STREQ(arg, "--partial-loads-ok=yes"))
//zz       clo_partial_loads_ok = True;
//zz    else if (VG_CLO_STREQ(arg, "--partial-loads-ok=no"))
//zz       clo_partial_loads_ok = False;
//zz 
//zz    else
//zz       return VG_(replacement_malloc_process_cmd_line_option)(arg);
//zz 
//zz    return True;
//zz }
//zz 
//zz void SK_(print_usage)(void)
//zz {
//zz    VG_(printf)(
//zz "    --partial-loads-ok=no|yes same as for Memcheck [yes]\n"
//zz    );
//zz    VG_(replacement_malloc_print_usage)();
//zz }
//zz 
//zz void SK_(print_debug_usage)(void)
//zz {
//zz    VG_(replacement_malloc_print_debug_usage)();
//zz }

/*------------------------------------------------------------*/
/*--- Segments                                             ---*/
/*------------------------------------------------------------*/

// Choose values that couldn't possibly be pointers
#define NONPTR          ((Seg)0xA1)
#define UNKNOWN         ((Seg)0xB2)
#define BOTTOM          ((Seg)0xC3)

static ISList* seglist = NULL;

// So that post_reg_write_clientcall knows the segment just allocated.
static Seg last_seg_added = NULL;

// Returns the added heap segment
static Seg add_new_segment ( ThreadId tid, 
                             Addr p, SizeT size, SegStatus status )
{
   ExeContext* where = VG_(record_ExeContext)( tid, 0/*first_ip_delta*/ );
   Seg         seg   = Seg__construct(p, size, where, status);

   last_seg_added = seg;

   ISList__insertI( seglist, seg );

   return seg;
}

// Forward declarations
static void copy_mem( Addr from, Addr to, SizeT len );
static void set_mem_unknown ( Addr a, SizeT len );

static __inline__
void* alloc_and_new_mem_heap ( ThreadId tid,
                               SizeT size, SizeT alignment, Bool is_zeroed )
{
   Addr p;

   if ( ((SSizeT)size) < 0) return NULL;

   p = (Addr)VG_(cli_malloc)(alignment, size);
   if (is_zeroed) VG_(memset)((void*)p, 0, size);

   set_mem_unknown( p, size );
   add_new_segment( tid, p, size, SegHeap );

   return (void*)p;
}

static void die_and_free_mem_heap ( ThreadId tid, Seg seg )
{
   // Empty and free the actual block, if on the heap (not necessary for
   // mmap segments).
   set_mem_unknown( Seg__a(seg), Seg__size(seg) );
   VG_(cli_free)( (void*)Seg__a(seg) );

   if (0 == Seg__size(seg)) {
      // XXX: can recycle Seg now
   }

   // Remember where freed
   Seg__heap_free( seg, 
                   VG_(record_ExeContext)( tid, 0/*first_ip_delta*/ ) );
}

//zz #if 0
//zz static void die_and_free_mem_munmap ( Seg seg/*, Seg* prev_chunks_next_ptr*/ )
//zz {
//zz    // Remember where freed
//zz    seg->where = VG_(get_ExeContext)( VG_(get_current_or_recent_tid)() );
//zz    sk_assert(SegMmap == seg->status);
//zz    seg->status = SegMmapFree;
//zz }
//zz #endif

static __inline__ void handle_free_heap( ThreadId tid, void* p )
{
   Seg seg;
   if ( ! ISList__findI0( seglist, (Addr)p, &seg ) )
      return;

   die_and_free_mem_heap( tid, seg );
}

//zz #if 0
//zz static void handle_free_munmap( void* p, UInt len )
//zz {
//zz    Seg seg;
//zz    if ( ! ISList__findI( seglist, (Addr)p, &seg ) ) {
//zz       VG_(skin_panic)("handle_free_munmap:  didn't find segment;\n"
//zz                       "should check all the mmap segment ranges, this\n"
//zz                       "one should be in them.  (Well, it's possible that\n"
//zz                       "it's not, but I'm not handling that case yet.)\n");
//zz    }
//zz    if (len != VG_ROUNDUP(Seg__size(seg), VKI_BYTES_PER_PAGE)) {
//zz //      if (seg->is_truncated_map && seg->size < len) {
//zz //         // ok
//zz //      } else {
//zz          VG_(printf)("len = %u, seg->size = %u\n", len, Seg__size(seg));
//zz          VG_(skin_panic)("handle_free_munmap:  length didn't match\n");
//zz //      }
//zz    }
//zz 
//zz    die_and_free_mem_munmap( seg/*, prev_chunks_next_ptr*/ );
//zz }
//zz #endif

/*------------------------------------------------------------*/
/*--- Shadow memory                                        ---*/
/*------------------------------------------------------------*/

/* Shadow memory holds one Seg for each naturally aligned (guest)
   word.  For a 32 bit target (assuming host word size == guest word
   size) that means one Seg per 4 bytes, and each Seg occupies 4
   bytes.  For a 64 bit target that means one Seg per 8 bytes, and
   each Seg occupies 8 bytes.  Hence in each case the overall space
   overhead for shadow memory is 1:1.

   This does however make it a bit tricky to size SecMap.vseg[], simce
   it needs to hold 16384 entries for 32 bit targets but only 8192
   entries for 64 bit targets. */

#if 0
__attribute__((unused))
static void pp_curr_ExeContext(void)
{
   VG_(pp_ExeContext)(
      VG_(get_ExeContext)(
         VG_(get_current_or_recent_tid)() ) );
   VG_(message)(Vg_UserMsg, "");
}
#endif

#if defined(VGA_x86) || defined(VGA_ppc32)
#  define SHMEM_SECMAP_MASK         0xFFFC
#  define SHMEM_SECMAP_SHIFT        2
#  define SHMEM_IS_WORD_ALIGNED(_a) VG_IS_4_ALIGNED(_a)
#  define SEC_MAP_WORDS             (0x10000UL / 4UL) /* 16k */
#elif defined(VGA_amd64) || defined(VGA_ppc64)
#  define SHMEM_SECMAP_MASK         0xFFF8
#  define SHMEM_SECMAP_SHIFT        3
#  define SHMEM_IS_WORD_ALIGNED(_a) VG_IS_8_ALIGNED(_a)
#  define SEC_MAP_WORDS             (0x10000UL / 8UL) /* 8k */
#else
#  error "Unknown arch"
#endif

typedef
   struct {
      Seg vseg[SEC_MAP_WORDS];
   }
   SecMap;

static SecMap  distinguished_secondary_map;

/* An entry in the primary map.  base must be a 64k-aligned value, and
   sm points at the relevant secondary map.  The secondary may be
   either a real secondary, or the distinguished secondary.  DO NOT
   CHANGE THIS LAYOUT: the first word has to be the key for OSet fast
   lookups.
*/
typedef
   struct {
      Addr    base;
      SecMap* sm;
   }
   PriMapEnt;

/* Primary map is an OSet of PriMapEnt (primap_L2), "fronted" by a
   cache (primap_L1). */

/* Tunable parameter: How big is the L1 queue? */
#define N_PRIMAP_L1 24

/* Tunable parameter: How far along the L1 queue to insert
   entries resulting from L2 lookups? */
#define PRIMAP_L1_INSERT_IX 12

static struct {
          Addr       base;
          PriMapEnt* ent; // pointer to the matching primap_L2 node
       }
       primap_L1[N_PRIMAP_L1];

static OSet* primap_L2 = NULL;


/* # searches initiated in auxmap_L1, and # base cmps required */
static ULong n_primap_L1_searches  = 0;
static ULong n_primap_L1_cmps      = 0;
/* # of searches that missed in auxmap_L1 and therefore had to
   be handed to auxmap_L2. And the number of nodes inserted. */
static ULong n_primap_L2_searches  = 0;
static ULong n_primap_L2_nodes     = 0;


static void init_shadow_memory ( void )
{
   Int i;

   for (i = 0; i < SEC_MAP_WORDS; i++)
      distinguished_secondary_map.vseg[i] = UNKNOWN;

   for (i = 0; i < N_PRIMAP_L1; i++) {
      primap_L1[i].base = 0;
      primap_L1[i].ent  = NULL;
   }

   tl_assert(0 == offsetof(PriMapEnt,base));
   tl_assert(sizeof(Addr) == sizeof(void*));
   primap_L2 = VG_(OSetGen_Create)( /*keyOff*/  offsetof(PriMapEnt,base),
                                    /*fastCmp*/ NULL,
                                    VG_(malloc), VG_(free) );
   tl_assert(primap_L2);
}

static void insert_into_primap_L1_at ( Word rank, PriMapEnt* ent )
{
   Word i;
   tl_assert(ent);
   tl_assert(rank >= 0 && rank < N_PRIMAP_L1);
   for (i = N_PRIMAP_L1-1; i > rank; i--)
      primap_L1[i] = primap_L1[i-1];
   primap_L1[rank].base = ent->base;
   primap_L1[rank].ent  = ent;
}

static inline PriMapEnt* maybe_find_in_primap ( Addr a )
{
   PriMapEnt  key;
   PriMapEnt* res;
   Word       i;

   a &= ~(Addr)0xFFFF;

   /* First search the front-cache, which is a self-organising
      list containing the most popular entries. */

   if (LIKELY(primap_L1[0].base == a))
      return primap_L1[0].ent;
   if (LIKELY(primap_L1[1].base == a)) {
      Addr       t_base = primap_L1[0].base;
      PriMapEnt* t_ent  = primap_L1[0].ent;
      primap_L1[0].base = primap_L1[1].base;
      primap_L1[0].ent  = primap_L1[1].ent;
      primap_L1[1].base = t_base;
      primap_L1[1].ent  = t_ent;
      return primap_L1[0].ent;
   }

   n_primap_L1_searches++;

   for (i = 0; i < N_PRIMAP_L1; i++) {
      if (primap_L1[i].base == a) {
         break;
      }
   }
   tl_assert(i >= 0 && i <= N_PRIMAP_L1);

   n_primap_L1_cmps += (ULong)(i+1);

   if (i < N_PRIMAP_L1) {
      if (i > 0) {
         Addr       t_base = primap_L1[i-1].base;
         PriMapEnt* t_ent  = primap_L1[i-1].ent;
         primap_L1[i-1].base = primap_L1[i-0].base;
         primap_L1[i-1].ent  = primap_L1[i-0].ent;
         primap_L1[i-0].base = t_base;
         primap_L1[i-0].ent  = t_ent;
         i--;
      }
      return primap_L1[i].ent;
   }

   n_primap_L2_searches++;

   /* First see if we already have it. */
   key.base = a;
   key.sm   = 0;

   res = VG_(OSetGen_Lookup)(primap_L2, &key);
   if (res)
      insert_into_primap_L1_at( PRIMAP_L1_INSERT_IX, res );
   return res;
}

static SecMap* alloc_secondary_map ( void )
{
   SecMap* map;
   UInt  i;

   // JRS 2008-June-25: what's the following assertion for?
   tl_assert(0 == (sizeof(SecMap) % VKI_MAX_PAGE_SIZE));

   map = VG_(am_shadow_alloc)( sizeof(SecMap) );
   if (map == NULL)
      VG_(out_of_memory_NORETURN)( "annelid:allocate new SecMap",
                                   sizeof(SecMap) );

   for (i = 0; i < SEC_MAP_WORDS; i++)
      map->vseg[i] = UNKNOWN;

   return map;
}

static PriMapEnt* find_or_alloc_in_primap ( Addr a )
{
   PriMapEnt *nyu, *res;

   /* First see if we already have it. */
   res = maybe_find_in_primap( a );
   if (LIKELY(res))
      return res;

   /* Ok, there's no entry in the secondary map, so we'll have
      to allocate one. */
   a &= ~(Addr)0xFFFF;

   nyu = (PriMapEnt*) VG_(OSetGen_AllocNode)( 
                         primap_L2, sizeof(PriMapEnt) );
   tl_assert(nyu);
   nyu->base = a;
   nyu->sm   = alloc_secondary_map();
   tl_assert(nyu->sm);
   VG_(OSetGen_Insert)( primap_L2, nyu );
   insert_into_primap_L1_at( PRIMAP_L1_INSERT_IX, nyu );
   n_primap_L2_nodes++;
   return nyu;
}

/////////////////////////////////////////////////

// Nb: 'a' must be naturally word aligned for the host.
static __inline__ Seg get_mem_vseg ( Addr a )
{
   SecMap* sm     = find_or_alloc_in_primap(a)->sm;
   UWord   sm_off = (a & SHMEM_SECMAP_MASK) >> SHMEM_SECMAP_SHIFT;
   tl_assert(SHMEM_IS_WORD_ALIGNED(a));
   return sm->vseg[sm_off];
}

// Nb: 'a' must be naturally word aligned for the host.
static __inline__ void set_mem_vseg ( Addr a, Seg vseg )
{
   SecMap* sm     = find_or_alloc_in_primap(a)->sm;
   UWord   sm_off = (a & SHMEM_SECMAP_MASK) >> SHMEM_SECMAP_SHIFT;
   tl_assert(SHMEM_IS_WORD_ALIGNED(a));
   sm->vseg[sm_off] = vseg;
}

//zz // Returns UNKNOWN if no matches.  Never returns BOTTOM or NONPTR.
//zz static Seg get_mem_aseg( Addr a )
//zz {
//zz    Seg aseg;
//zz    Bool is_found;
//zz 
//zz    VGP_PUSHCC(VgpGetMemAseg);
//zz    is_found = ISList__findI( seglist, a, &aseg );
//zz    VGP_POPCC(VgpGetMemAseg);
//zz    return ( is_found ? aseg : UNKNOWN );
//zz }

/*--------------------------------------------------------------------*/
/*--- Error handling                                               ---*/
/*--------------------------------------------------------------------*/

typedef
   enum {
      /* Possible data race */
      LoadStoreSupp,
      ArithSupp,
      SysParamSupp,
   }
   AnnelidSuppKind;

/* What kind of error it is. */
typedef
   enum {
      LoadStoreErr,     // mismatched ptr/addr segments on load/store
      ArithErr,         // bad arithmetic between two segment pointers
      SysParamErr,      // block straddling >1 segment passed to syscall
   }
   AnnelidErrorKind;


// These ones called from generated code.

typedef
   struct {
      Addr a;
      UInt size;
      Seg  vseg;
      Bool is_write;
   }
   LoadStoreExtra;

typedef
   struct {
      Seg seg1;
      Seg seg2;
      const HChar* opname; // user-understandable text name
   }
   ArithExtra;

typedef
   struct {
      CorePart part;
      Addr lo;
      Addr hi;
      Seg  seglo;
      Seg  seghi;
   }
   SysParamExtra;

//zz static UInt actual_arg1, actual_arg2/*, actual_res*/;

static
void record_loadstore_error( Addr a, UInt size, Seg vseg, Bool is_write )
{
   LoadStoreExtra extra = {
      .a = a, .size = size, .vseg = vseg, .is_write = is_write
   };
   VG_(maybe_record_error)( VG_(get_running_tid)(), LoadStoreErr,
                            /*a*/0, /*str*/NULL, /*extra*/(void*)&extra);
}

static void
record_arith_error( Seg seg1, Seg seg2, HChar* opname )
{
   ArithExtra extra = {
      .seg1 = seg1, .seg2 = seg2, .opname = opname
   };
   VG_(maybe_record_error)( VG_(get_running_tid)(), ArithErr,
                            /*a*/0, /*str*/NULL, /*extra*/(void*)&extra);
}

static void record_sysparam_error( ThreadId tid, CorePart part, Char* s,
                                   Addr lo, Addr hi, Seg seglo, Seg seghi )
{
   SysParamExtra extra = {
      .part = part, .lo = lo, .hi = hi, .seglo = seglo, .seghi = seghi
   };
   VG_(maybe_record_error)( tid, SysParamErr, /*a*/(Addr)0, /*str*/s,
                            /*extra*/(void*)&extra);
}

static Bool eq_Error ( VgRes res, Error* e1, Error* e2 )
{
   tl_assert(VG_(get_error_kind)(e1) == VG_(get_error_kind)(e2));

   // Nb: ok to compare string pointers, rather than string contents,
   // because the same static strings are shared.

   switch (VG_(get_error_kind)(e1)) {

   case LoadStoreErr:
   case SysParamErr:
      return VG_STREQ(VG_(get_error_string)(e1), VG_(get_error_string)(e2));

   case ArithErr:
      return True;

   default:
      VG_(tool_panic)("eq_Error: unrecognised error kind");
   }
}

static Char* readwrite(Bool is_write)
{
   return ( is_write ? "write" : "read" );
}

static inline
Bool is_known_segment(Seg seg)
{
   return (UNKNOWN != seg && BOTTOM != seg && NONPTR != seg);
}

//zz static Char* an_name_UOpcode(Int opc)
//zz {
//zz    if (opc >= 0) return VG_(name_UOpcode)(True, opc);
//zz 
//zz    else if (-opc == VGOFF_(helper_mul_32_64))    return  "MUL";
//zz    else if (-opc == VGOFF_(helper_imul_32_64))   return "IMUL";
//zz    else if (-opc == VGOFF_(helper_div_64_32))    return  "DIV";
//zz    else if (-opc == VGOFF_(helper_idiv_64_32))   return "IDIV";
//zz 
//zz    else VG_(skin_panic)("an_name_UOpcode");
//zz }
      
static void pp_Error ( Error* err )
{
   switch (VG_(get_error_kind)(err)) {
   //----------------------------------------------------------
   case LoadStoreErr: {
      LoadStoreExtra* extra = (LoadStoreExtra*)VG_(get_error_extra)(err);
      Char           *place, *legit, *how_invalid;
      Addr            a    = extra->a;
      Seg             vseg = extra->vseg;

      tl_assert(is_known_segment(vseg) || NONPTR == vseg);

      if (NONPTR == vseg) {
         // Access via a non-pointer
         VG_(message)(Vg_UserMsg, "Invalid %s of size %u",
                                   readwrite(extra->is_write), extra->size);
         VG_(pp_ExeContext)( VG_(get_error_where)(err) );
         VG_(message)(Vg_UserMsg,
                      " Address %p is not derived from any known block", a);

      } else {
         // Access via a pointer, but outside its range.
         Int cmp;
         Word miss_size;
         Seg__cmp(vseg, a, &cmp, &miss_size);
         if      (cmp  < 0) place = "before";
         else if (cmp == 0) place = "inside";
         else               place = "after";
         how_invalid = ( ( Seg__is_freed(vseg) && 0 != cmp )
                       ? "Doubly-invalid" : "Invalid" );
         legit = ( Seg__is_freed(vseg) ? "once-" : "" );

         VG_(message)(Vg_UserMsg, "%s %s of size %u", how_invalid,
                                  readwrite(extra->is_write), extra->size);
         VG_(pp_ExeContext)( VG_(get_error_where)(err) );

         VG_(message)(Vg_UserMsg,
                      " Address %p is %ld bytes %s the accessing pointer's",
                      a, miss_size, place);
         VG_(message)(Vg_UserMsg,
                    " %slegitimate range, a block of size %ld %s",
                      legit, Seg__size(vseg), Seg__status_str(vseg) );
         VG_(pp_ExeContext)(Seg__where(vseg));
      }
      break;
   }

   //----------------------------------------------------------
   case ArithErr: {
      ArithExtra* extra = VG_(get_error_extra)(err);
      Seg    seg1   = extra->seg1;
      Seg    seg2   = extra->seg2;
      Char*  which;

      tl_assert(BOTTOM != seg1);
      tl_assert(BOTTOM != seg2 && UNKNOWN != seg2);

      VG_(message)(Vg_UserMsg, "Invalid arguments to %s", extra->opname);
      VG_(pp_ExeContext)( VG_(get_error_where)(err) );

      if (seg1 != seg2) {
         if (NONPTR == seg1) {
            VG_(message)(Vg_UserMsg, " First arg not a pointer");
         } else if (UNKNOWN == seg1) {
            VG_(message)(Vg_UserMsg, " First arg may be a pointer");
         } else {
            VG_(message)(Vg_UserMsg, " First arg derived from address %p of "
                                     "%d-byte block %s",
                                     Seg__a(seg1), Seg__size(seg1),
                                     Seg__status_str(seg1) );
            VG_(pp_ExeContext)(Seg__where(seg1));
         }
         which = "Second arg";
      } else {
         which = "Both args";
      }
      if (NONPTR == seg2) {
         VG_(message)(Vg_UserMsg, " %s not a pointer", which);
      } else {
         VG_(message)(Vg_UserMsg, " %s derived from address %p of "
                                  "%d-byte block %s",
                                  which, Seg__a(seg2), Seg__size(seg2),
                                  Seg__status_str(seg2));
         VG_(pp_ExeContext)(Seg__where(seg2));
      }
      break;
   }

//zz    //----------------------------------------------------------
//zz    case SysParamErr: {
//zz       SysParamExtra* extra = (SysParamExtra*)VG_(get_error_extra)(err);
//zz       Addr           lo    = extra->lo;
//zz       Addr           hi    = extra->hi;
//zz       Seg            seglo = extra->seglo;
//zz       Seg            seghi = extra->seghi;
//zz       Char*          s     = VG_(get_error_string) (err);
//zz       Char*          what;
//zz 
//zz       sk_assert(BOTTOM != seglo && BOTTOM != seghi);
//zz 
//zz       if      (Vg_CoreSysCall == extra->part) what = "Syscall param ";
//zz       else if (Vg_CorePThread == extra->part) what = "";
//zz       else    VG_(skin_panic)("bad CorePart");
//zz 
//zz       if (seglo == seghi) {
//zz          // freed block
//zz          sk_assert(is_known_segment(seglo) && Seg__is_freed(seglo));
//zz          VG_(message)(Vg_UserMsg, "%s%s contains unaddressable byte(s)", what, s);
//zz          VG_(pp_ExeContext)( VG_(get_error_where)(err) );
//zz 
//zz          VG_(message)(Vg_UserMsg, "Address %p is %d bytes within a "
//zz                                   "%d-byte block %s",
//zz                                   lo, lo-Seg__a(seglo), Seg__size(seglo),
//zz                                   Seg__status_str(seglo) );
//zz          VG_(pp_ExeContext)(Seg__where(seglo));
//zz 
//zz       } else {
//zz          // mismatch
//zz          VG_(message)(Vg_UserMsg, "%s%s is non-contiguous", what, s);
//zz          VG_(pp_ExeContext)( VG_(get_error_where)(err) );
//zz 
//zz          if (UNKNOWN == seglo) {
//zz             VG_(message)(Vg_UserMsg, "First byte is not within a known block");
//zz          } else {
//zz             VG_(message)(Vg_UserMsg, "First byte (%p) is %d bytes within a "
//zz                                      "%d-byte block %s",
//zz                                      lo, lo-Seg__a(seglo), Seg__size(seglo),
//zz                                      Seg__status_str(seglo) );
//zz             VG_(pp_ExeContext)(Seg__where(seglo));
//zz          }
//zz 
//zz          if (UNKNOWN == seghi) {
//zz             VG_(message)(Vg_UserMsg, "Last byte is not within a known block");
//zz          } else {
//zz             VG_(message)(Vg_UserMsg, "Last byte (%p) is %d bytes within a "
//zz                                      "%d-byte block %s",
//zz                                      hi, hi-Seg__a(seghi), Seg__size(seghi),
//zz                                      Seg__status_str(seghi));
//zz             VG_(pp_ExeContext)(Seg__where(seghi));
//zz          }
//zz       }
//zz       break;
//zz    }

   default:
      VG_(tool_panic)("pp_Error: unrecognised error kind");
   }
}

static UInt update_Error_extra ( Error* err )
{
   switch (VG_(get_error_kind)(err)) {
   case LoadStoreErr: return sizeof(LoadStoreExtra);
   case ArithErr:     return 0;
   case SysParamErr:  return sizeof(SysParamExtra);
   default:           VG_(tool_panic)("update_extra");
   }
}

static Bool is_recognised_suppression ( Char* name, Supp *su )
{
   SuppKind skind;

   if      (VG_STREQ(name, "LoadStore"))  skind = LoadStoreSupp;
   else if (VG_STREQ(name, "Arith"))      skind = ArithSupp;
   else if (VG_STREQ(name, "SysParam"))   skind = SysParamSupp;
   else
      return False;

   VG_(set_supp_kind)(su, skind);
   return True;
}

static Bool read_extra_suppression_info ( Int fd, Char* buf, 
                                          Int nBuf, Supp* su )
{
   Bool eof;

   if (VG_(get_supp_kind)(su) == SysParamSupp) {
      eof = VG_(get_line) ( fd, buf, nBuf );
      if (eof) return False;
      VG_(set_supp_string)(su, VG_(strdup)(buf));
   }
   return True;
}

static Bool error_matches_suppression (Error* err, Supp* su)
{
   ErrorKind  ekind     = VG_(get_error_kind )(err);

   switch (VG_(get_supp_kind)(su)) {
   case LoadStoreSupp:  return (ekind == LoadStoreErr);
   case ArithSupp:      return (ekind == ArithErr);
   case SysParamSupp:   return (ekind == SysParamErr);
   default:
      VG_(printf)("Error:\n"
                  "  unknown suppression type %d\n",
                  VG_(get_supp_kind)(su));
      VG_(tool_panic)("unknown suppression type in "
                      "SK_(error_matches_suppression)");
   }
}

static Char* get_error_name ( Error* err )
{
   switch (VG_(get_error_kind)(err)) {
   case LoadStoreErr:       return "LoadStore";
   case ArithErr:           return "Arith";
   case SysParamErr:        return "SysParam";
   default:                 VG_(tool_panic)("get_error_name: unexpected type");
   }
}

static void print_extra_suppression_info ( Error* err )
{
   if (SysParamErr == VG_(get_error_kind)(err)) {
      VG_(printf)("   %s\n", VG_(get_error_string)(err));
   }
}

/*------------------------------------------------------------*/
/*--- malloc() et al replacements                          ---*/
/*------------------------------------------------------------*/

static void* an_replace_malloc ( ThreadId tid, SizeT n )
{
   return alloc_and_new_mem_heap ( tid, n, VG_(clo_alignment),
                                        /*is_zeroed*/False );
}

static void* an_replace___builtin_new ( ThreadId tid, SizeT n )
{
   return alloc_and_new_mem_heap ( tid, n, VG_(clo_alignment),
                                           /*is_zeroed*/False );
}

static void* an_replace___builtin_vec_new ( ThreadId tid, SizeT n )
{
   return alloc_and_new_mem_heap ( tid, n, VG_(clo_alignment),
                                           /*is_zeroed*/False );
}

static void* an_replace_memalign ( ThreadId tid, SizeT align, SizeT n )
{
   return alloc_and_new_mem_heap ( tid, n, align,
                                        /*is_zeroed*/False );
}

static void* an_replace_calloc ( ThreadId tid, SizeT nmemb, SizeT size1 )
{
   return alloc_and_new_mem_heap ( tid, nmemb*size1, VG_(clo_alignment),
                                        /*is_zeroed*/True );
}

static void an_replace_free ( ThreadId tid, void* p )
{
   // Should arguably check here if p.vseg matches the segID of the
   // pointed-to block... unfortunately, by this stage, we don't know what
   // p.vseg is, because we don't know the address of p (the p here is a
   // copy, and we've lost the address of its source).  To do so would
   // require passing &p in, which would require rewriting part of
   // vg_replace_malloc.c... argh.
   //
   // However, Memcheck does free checking, and will catch almost all
   // violations this checking would have caught.  (Would only miss if we
   // unluckily passed an unrelated pointer to the very start of a heap
   // block that was unrelated to that block.  This is very unlikely!)    So
   // we haven't lost much.

   handle_free_heap(tid, p);
}

static void an_replace___builtin_delete ( ThreadId tid, void* p )
{
   handle_free_heap(tid, p);
}

static void an_replace___builtin_vec_delete ( ThreadId tid, void* p )
{
   handle_free_heap(tid, p);
}

static void* an_replace_realloc ( ThreadId tid, void* p_old, SizeT new_size )
{
   Seg seg;

   /* First try and find the block. */
   if ( ! ISList__findI0( seglist, (Addr)p_old, &seg ) )
      return NULL;

   tl_assert(Seg__a(seg) == (Addr)p_old);

   if (new_size <= Seg__size(seg)) {
      /* new size is smaller */
      tl_assert(new_size > 0);
      set_mem_unknown( Seg__a(seg)+new_size, Seg__size(seg)-new_size );
      Seg__resize(seg, new_size, 
                  VG_(record_ExeContext)( tid, 0/*first_ip_delta*/ ) );
      last_seg_added = seg;      // necessary for post_reg_write_clientcall
      return p_old;

   } else {
      /* new size is bigger: allocate, copy from old to new */
      Addr p_new = (Addr)VG_(cli_malloc)(VG_(clo_alignment), new_size);
      VG_(memcpy)((void*)p_new, p_old, Seg__size(seg));

      /* Notification: first half kept and copied, second half new */
      copy_mem       ( (Addr)p_old, p_new, Seg__size(seg) );
      set_mem_unknown( p_new+Seg__size(seg), new_size-Seg__size(seg) );

      /* Free old memory */
      die_and_free_mem_heap( tid, seg );

      /* This has to be after die_and_free_mem_heap, otherwise the
         former succeeds in shorting out the new block, not the
         old, in the case when both are on the same list.  */
      add_new_segment ( tid, p_new, new_size, SegHeap );

      return (void*)p_new;
   }
}

/*------------------------------------------------------------*/
/*--- Memory events                                        ---*/
/*------------------------------------------------------------*/

static __inline__
void set_mem( Addr a, SizeT len, Seg seg )
{
   Addr end;

   if (0 == len)
      return;

   if (len > 100 * 1000 * 1000)
      VG_(message)(Vg_UserMsg,
                   "Warning: set address range state: large range %d", len);

   a   = VG_ROUNDDN(a,       sizeof(UWord));
   end = VG_ROUNDUP(a + len, sizeof(UWord));
   for ( ; a < end; a += sizeof(UWord))
      set_mem_vseg(a, seg);
}

static void set_mem_unknown( Addr a, SizeT len )
{
   set_mem( a, len, UNKNOWN );
}

//zz static void set_mem_nonptr( Addr a, UInt len )
//zz {
//zz    set_mem( a, len, NONPTR );
//zz }

static void new_mem_startup( Addr a, SizeT len, Bool rr, Bool ww, Bool xx )
{
   set_mem_unknown( a, len );
}

//zz // XXX: Currently not doing anything with brk() -- new segments, or not?
//zz // Proper way to do it would be to grow/shrink a single, special brk segment.
//zz //
//zz // brk is difficult: it defines a single segment, of changeable size.
//zz // It starts off with size zero, at the address given by brk(0).  There are
//zz // no pointers within the program to it.  Any subsequent calls by the
//zz // program to brk() (possibly growing or shrinking it) return pointers to
//zz // the *end* of the segment (nb: this is the kernel brk(), which is
//zz // different to the libc brk()).
//zz //
//zz // If fixing this, don't forget to update the brk case in SK_(post_syscall).
//zz //
//zz // Nb: not sure if the return value is the last byte addressible, or one
//zz // past the end of the segment.
//zz //
//zz static void new_mem_brk( Addr a, UInt len )
//zz {
//zz    set_mem_unknown(a, len);
//zz    //VG_(skin_panic)("can't handle new_mem_brk");
//zz }

// Not quite right:  if you mmap a segment into a specified place, it could
// be legitimate to do certain arithmetic with the pointer that it wouldn't
// otherwise.  Hopefully this is rare, though.
static void new_mem_mmap( Addr a, SizeT len, Bool rr, Bool ww, Bool xx )
{
//zz #if 0
//zz    Seg seg = NULL;
//zz 
//zz    // Check for overlapping segments
//zz #if 0
//zz    is_overlapping_seg___a   = a;    // 'free' variable
//zz    is_overlapping_seg___len = len;  // 'free' variable
//zz    seg = (Seg)VG_(HT_first_match) ( mlist, is_overlapping_seg );
//zz    is_overlapping_seg___a   = 0;    // paranoia, reset
//zz    is_overlapping_seg___len = 0;    // paranoia, reset
//zz #endif
//zz 
//zz    // XXX: do this check properly with ISLists
//zz 
//zz    if ( ISList__findI( seglist, a, &seg )) {
//zz       sk_assert(SegMmap == seg->status || SegMmapFree == seg->status);
//zz       if (SegMmap == seg->status)
//zz    
//zz    }
//zz 
//zz    if (NULL != seg) {
//zz       // Right, we found an overlap
//zz       if (VG_(clo_verbosity) > 1)
//zz          VG_(message)(Vg_UserMsg, "mmap overlap:  old: %p, %d;  new: %p, %d",
//zz                                   seg->left, Seg__size(seg), a, len);
//zz       if (seg->left <= a && a <= seg->right) {
//zz          // New one truncates end of the old one.  Nb: we don't adjust its
//zz          // size, because the first segment's pointer can be (and for
//zz          // Konqueror, is) legitimately used to access parts of the second
//zz          // segment.  At least, I assume Konqueror is doing something legal.
//zz          // so that a size mismatch upon munmap isn't a problem.
//zz //         seg->size = a - seg->data;
//zz //         seg->is_truncated_map = True;
//zz //         if (VG_(clo_verbosity) > 1)
//zz //            VG_(message)(Vg_UserMsg, "old seg truncated to length %d",
//zz //                                     seg->size);
//zz       } else {
//zz          VG_(skin_panic)("Can't handle this mmap() overlap case");
//zz       }
//zz    }
   set_mem_unknown( a, len );
   add_new_segment( VG_(get_running_tid)(), a, len, SegMmap );
//zz #endif
}

static void copy_mem( Addr from, Addr to, SizeT len )
{
   Addr fromend = from + len;

   // Must be aligned due to malloc always returning aligned objects.
   tl_assert(VG_IS_8_ALIGNED(from) && VG_IS_8_ALIGNED(to));

   // Must only be called with positive len.
   if (0 == len)
      return;

   for ( ; from < fromend; from += sizeof(UWord), to += sizeof(UWord))
      set_mem_vseg( to, get_mem_vseg(from) );
}

//zz // Similar to SK_(realloc)()
//zz static void copy_mem_remap( Addr from, Addr to, UInt len )
//zz {
//zz    VG_(skin_panic)("argh: copy_mem_remap");
//zz }
//zz 
//zz static void die_mem_brk( Addr a, UInt len )
//zz {
//zz    set_mem_unknown(a, len);
//zz //   VG_(skin_panic)("can't handle die_mem_brk()");
//zz }

static void die_mem_munmap( Addr a, SizeT len )
{
//   handle_free_munmap( (void*)a, len );
}

//zz // Don't need to check all addresses within the block; in the absence of
//zz // discontiguous segments, the segments for the first and last bytes should
//zz // be the same.  Can't easily check the pointer segment matches the block
//zz // segment, unfortunately, but the first/last check should catch most
//zz // errors.
//zz static void pre_mem_access2 ( CorePart part, ThreadId tid, Char* s,
//zz                               Addr lo, Addr hi )
//zz {
//zz    Seg seglo, seghi;
//zz 
//zz    // Don't check code being translated -- very slow, and not much point
//zz    if (Vg_CoreTranslate == part) return;
//zz 
//zz    // Don't check the signal case -- only happens in core, no need to check
//zz    if (Vg_CoreSignal == part) return;
//zz 
//zz    // Check first and last bytes match
//zz    seglo = get_mem_aseg( lo );
//zz    seghi = get_mem_aseg( hi );
//zz    sk_assert( BOTTOM != seglo && NONPTR != seglo );
//zz    sk_assert( BOTTOM != seghi && NONPTR != seghi );
//zz 
//zz    if ( seglo != seghi || (UNKNOWN != seglo && Seg__is_freed(seglo)) ) {
//zz       // First/last bytes don't match, or seg has been freed.
//zz       switch (part) {
//zz       case Vg_CoreSysCall:
//zz       case Vg_CorePThread:
//zz          record_sysparam_error(tid, part, s, lo, hi, seglo, seghi);
//zz          break;
//zz 
//zz       default:
//zz          VG_(printf)("part = %d\n", part);
//zz          VG_(skin_panic)("unknown corepart in pre_mem_access2");
//zz       }
//zz    }
//zz }
//zz 
//zz static void pre_mem_access ( CorePart part, ThreadId tid, Char* s,
//zz                              Addr base, UInt size )
//zz {
//zz    pre_mem_access2( part, tid, s, base, base + size - 1 );
//zz }
//zz 
//zz static
//zz void pre_mem_read_asciiz ( CorePart part, ThreadId tid, Char* s, Addr lo )
//zz {
//zz    Addr hi = lo;
//zz 
//zz    // Nb: the '\0' must be included in the lo...hi range
//zz    while ('\0' != *(Char*)hi) hi++;
//zz    pre_mem_access2( part, tid, s, lo, hi );
//zz }
//zz 
//zz static void post_mem_write(Addr a, UInt len)
//zz {
//zz    set_mem_unknown(a, len);
//zz }
//zz 
//zz /*------------------------------------------------------------*/
//zz /*--- Register event handlers                              ---*/
//zz /*------------------------------------------------------------*/
//zz 
//zz static void post_regs_write_init ( void )
//zz {
//zz    UInt i;
//zz    for (i = R_EAX; i <= R_EDI; i++)
//zz       VG_(set_shadow_archreg)( i, (UInt)UNKNOWN );
//zz 
//zz    // Don't bother about eflags
//zz }

// BEGIN move this uglyness to an_machine.c

static inline Bool host_is_big_endian ( void ) {
   UInt x = 0x11223344;
   return 0x1122 == *(UShort*)(&x);
}
static inline Bool host_is_little_endian ( void ) {
   UInt x = 0x11223344;
   return 0x3344 == *(UShort*)(&x);
}

#define N_INTREGINFO_OFFSETS 4

/* Holds the result of a query to 'get_IntRegInfo'.  Valid values for
   n_offsets are:

   -1: means the queried guest state slice exactly matches
       one integer register

   0: means the queried guest state slice does not overlap any
      integer registers

   1 .. N_INTREGINFO_OFFSETS: means the queried guest state offset
      overlaps n_offsets different integer registers, and their base
      offsets are placed in the offsets array.
*/
typedef
   struct {
      Int offsets[N_INTREGINFO_OFFSETS];
      Int n_offsets;
   }
   IntRegInfo;


#if defined(VGA_x86)
# include "libvex_guest_x86.h"
# define MC_SIZEOF_GUEST_STATE sizeof(VexGuestX86State)
#endif

#if defined(VGA_amd64)
# include "libvex_guest_amd64.h"
# define MC_SIZEOF_GUEST_STATE sizeof(VexGuestAMD64State)
#endif

/* See description on definition of type IntRegInfo. */
static void get_IntRegInfo ( /*OUT*/IntRegInfo* iii, Int offset, Int szB )
{
   Int o  = offset;
   Int sz = szB;
   /* Set default state 'does not intersect any int register. */
   VG_(memset)( iii, 0, sizeof(*iii) );

   /* --------------------- x86 --------------------- */

#  if defined(VGA_x86)

#  define GOF(_fieldname) \
      (offsetof(VexGuestX86State,guest_##_fieldname))

   Bool is4  = sz == 4;
   Bool is21 = sz == 2 || sz == 1;
   tl_assert(sz > 0);
   tl_assert(host_is_little_endian());
   if (o == GOF(EAX)     && is4) goto exactly1;
   if (o == GOF(ECX)     && is4) goto exactly1;
   if (o == GOF(EDX)     && is4) goto exactly1;
   if (o == GOF(EBX)     && is4) goto exactly1;
   if (o == GOF(ESP)     && is4) goto exactly1;
   if (o == GOF(EBP)     && is4) goto exactly1;
   if (o == GOF(ESI)     && is4) goto exactly1;
   if (o == GOF(EDI)     && is4) goto exactly1;
   if (o == GOF(EIP)     && is4) goto none;
   if (o == GOF(CC_OP)   && is4) goto none;
   if (o == GOF(CC_DEP1) && is4) goto none;
   if (o == GOF(CC_DEP2) && is4) goto none;
   if (o == GOF(CC_NDEP) && is4) goto none;
   if (o == GOF(DFLAG)   && is4) goto none;

   if (o == GOF(EAX)     && is21) {         o -= 0; goto contains_o; }
   if (o == GOF(EAX)+1   && is21) { o -= 1; o -= 0; goto contains_o; }
   if (o == GOF(ECX)     && is21) {         o -= 0; goto contains_o; }
   if (o == GOF(ECX)+1   && is21) { o -= 1; o -= 0; goto contains_o; }
   if (o == GOF(EBX)     && is21) {         o -= 0; goto contains_o; }
   if (o == GOF(EDX)     && is21) {         o -= 0; goto contains_o; }
   if (o == GOF(EDX)+1   && is21) { o -= 1; o -= 0; goto contains_o; }
   if (o == GOF(ESI)     && is21) {         o -= 0; goto contains_o; }
   if (o == GOF(EDI)     && is21) {         o -= 0; goto contains_o; }

   if (o == GOF(GS) && sz == 2) goto none;
   if (o == GOF(LDT) && is4) goto none;
   if (o == GOF(GDT) && is4) goto none;

   VG_(printf)("get_IntRegInfo(x86):failing on (%d,%d)\n", o, sz);
   tl_assert(0);
#  undef GOF

   /* -------------------- amd64 -------------------- */

#  elif defined(VGA_amd64)

#  define GOF(_fieldname) \
      (offsetof(VexGuestAMD64State,guest_##_fieldname))

   Bool is8   = sz == 8;
   Bool is421 = sz == 4 || sz == 2 || sz == 1;
   tl_assert(sz > 0);
   tl_assert(host_is_little_endian());

   if (o == GOF(RAX)     && is8) goto exactly1;
   if (o == GOF(RCX)     && is8) goto exactly1;
   if (o == GOF(RDX)     && is8) goto exactly1;
   if (o == GOF(RBX)     && is8) goto exactly1;
   if (o == GOF(RSP)     && is8) goto exactly1;
   if (o == GOF(RBP)     && is8) goto exactly1;
   if (o == GOF(RSI)     && is8) goto exactly1;
   if (o == GOF(RDI)     && is8) goto exactly1;
   if (o == GOF(R8)      && is8) goto exactly1;
   if (o == GOF(R9)      && is8) goto exactly1;
   if (o == GOF(R10)     && is8) goto exactly1;
   if (o == GOF(R11)     && is8) goto exactly1;
   if (o == GOF(R12)     && is8) goto exactly1;
   if (o == GOF(R13)     && is8) goto exactly1;
   if (o == GOF(R14)     && is8) goto exactly1;
   if (o == GOF(R15)     && is8) goto exactly1;
   if (o == GOF(RIP)     && is8) goto exactly1;
   if (o == GOF(CC_OP)   && is8) goto none;
   if (o == GOF(CC_DEP1) && is8) goto none;
   if (o == GOF(CC_DEP2) && is8) goto none;
   if (o == GOF(CC_NDEP) && is8) goto none;
   if (o == GOF(DFLAG)   && is8) goto none;

   if (o == GOF(RAX)     && is421) {         o -= 0; goto contains_o; }
   if (o == GOF(RAX)+1   && is421) { o -= 1; o -= 0; goto contains_o; }
   if (o == GOF(RCX)     && is421) {         o -= 0; goto contains_o; }
   if (o == GOF(RDX)     && is421) {         o -= 0; goto contains_o; }
   if (o == GOF(RBX)     && is421) {         o -= 0; goto contains_o; }
   if (o == GOF(RBP)     && is421) {         o -= 0; goto contains_o; }
   if (o == GOF(RSI)     && is421) {         o -= 0; goto contains_o; }
   if (o == GOF(R9)      && is421) {         o -= 0; goto contains_o; }
   if (o == GOF(R12)     && is421) {         o -= 0; goto contains_o; }
   if (o == GOF(R13)     && is421) {         o -= 0; goto contains_o; }
   if (o == GOF(R14)     && is421) {         o -= 0; goto contains_o; }
   if (o == GOF(R15)     && is421) {         o -= 0; goto contains_o; }

   if (o == GOF(FS_ZERO) && is8) goto none;

   VG_(printf)("get_IntRegInfo(amd64):failing on (%d,%d)\n", o, sz);
   tl_assert(0);
#  undef GOF


#  else
#    error "FIXME: not implemented for this architecture"
#  endif

  exactly1:
   iii->n_offsets = -1;
   return;
  none:
   iii->n_offsets = 0;
   return;
  contains_o:
   tl_assert(o >= 0 && 0 == (o % sizeof(UWord)));
   iii->n_offsets = 1;
   iii->offsets[0] = o;
   return;
}

// END move this uglyness to an_machine.c

/* returns True iff given slice exactly matches an int reg.  Merely
   a convenience wrapper around get_IntRegInfo. */
static Bool is_integer_guest_reg ( Int offset, Int szB )
{
   IntRegInfo iii;
   get_IntRegInfo( &iii, offset, szB );
   tl_assert(iii.n_offsets >= -1 && iii.n_offsets <= N_INTREGINFO_OFFSETS);
   return iii.n_offsets == -1;
}

/* these assume guest and host have the same endianness and
   word size (probably). */
static UWord get_guest_intreg ( ThreadId tid, Int shadowNo,
                                OffT offset, SizeT size )
{
   UChar tmp[ 2 + sizeof(UWord) ];
   tl_assert(size == sizeof(UWord));
   tl_assert(0 == (offset % sizeof(UWord)));
   VG_(memset)(tmp, 0, sizeof(tmp));
   tmp[0] = 0x31;
   tmp[ sizeof(tmp)-1 ] = 0x27;
   VG_(get_shadow_regs_area)(tid, &tmp[1], shadowNo, offset, size);
   tl_assert(tmp[0] == 0x31);
   tl_assert(tmp[ sizeof(tmp)-1 ] == 0x27);
   return * ((UWord*) &tmp[1] ); /* MISALIGNED LOAD */
}
static void put_guest_intreg ( ThreadId tid, Int shadowNo,
                               OffT offset, SizeT size, UWord w )
{
   tl_assert(size == sizeof(UWord));
   tl_assert(0 == (offset % sizeof(UWord)));
   VG_(set_shadow_regs_area)(tid, shadowNo, offset, size,
                             (const UChar*)&w);
}

/* Initialise the integer shadow registers to UNKNOWN.  This is a bit
   of a nasty kludge, but it does mean we don't need to know which
   registers we really need to initialise -- simply assume that all
   integer registers will be naturally aligned w.r.t. the start of the
   guest state, and fill in all possible entries. */
static void init_shadow_registers ( ThreadId tid )
{
   Int i, wordSzB = sizeof(UWord);
   for (i = 0; i < MC_SIZEOF_GUEST_STATE-wordSzB; i += wordSzB) {
      put_guest_intreg( tid, 1, i, wordSzB, (UWord)UNKNOWN );
   }
}

static void post_reg_write_nonptr ( ThreadId tid, OffT offset, SizeT size )
{
   // syscall_return: Default is non-pointer.  If it really is a pointer
   // (eg. for mmap()), SK_(post_syscall) sets it again afterwards.
   //
   // clientreq_return: All the global client requests return non-pointers
   // (except possibly CLIENT_CALL[0123], but they're handled by
   // post_reg_write_clientcall, not here).
   //
   if (is_integer_guest_reg( (Int)offset, (Int)size )) {
      put_guest_intreg( tid, 1, offset, size, (UWord)NONPTR );
   } else {
      tl_assert(0);
   }
   //   VG_(set_thread_shadow_archreg)( tid, reg, (UInt)NONPTR );
}

//zz static void post_reg_write_nonptr_or_unknown(ThreadId tid, UInt reg)
//zz {
//zz    // deliver_signal: called from two places; one sets the reg to zero, the
//zz    // other sets the stack pointer.
//zz    //
//zz    // pthread_return: All the pthread_* functions return non-pointers,
//zz    // except pthread_getspecific(), but it's ok: even though the
//zz    // allocation/getting of the specifics pointer happens on the real CPU,
//zz    // the set/get of the specific value is done in vg_libpthread.c on the
//zz    // simd CPU, using the specifics pointer.
//zz    //
//zz    // The MALLOC request is also (unfortunately) lumped in with the pthread
//zz    // ops... it does most certainly return a pointer, and one to a heap
//zz    // block, which is marked as UNKNOWN.  Inaccurately marking it is not
//zz    // really a problem, as the heap-blocks are entirely local to
//zz    // vg_libpthread.c.
//zz    //
//zz    Seg seg = ( VG_(get_thread_archreg)(tid, reg) ? UNKNOWN : NONPTR );
//zz    VG_(set_thread_shadow_archreg)( tid, reg, (UInt)seg );
//zz }

static
void post_reg_write_demux ( CorePart part, ThreadId tid,
                            OffT guest_state_offset, SizeT size)
{
   if (0)
   VG_(printf)("post_reg_write_demux: tid %d part %d off %ld size %ld\n",
               (Int)tid, (Int)part,
              guest_state_offset, size);
   switch (part) {
      case Vg_CoreStartup:
         /* This is a bit of a kludge since for any Vg_CoreStartup
            event we overwrite the entire shadow register set.  But
            that's ok - we're only called once with
            part==Vg_CoreStartup event, and in that case the supplied
            offset & size cover the entire guest state anyway. */
         init_shadow_registers(tid);
         break;
      case Vg_CoreSysCall:
         post_reg_write_nonptr( tid, guest_state_offset, size );
         break;
      case Vg_CoreClientReq:
         post_reg_write_nonptr( tid, guest_state_offset, size );
         break;
      default:
         tl_assert(0);
   }
}

static 
void post_reg_write_clientcall(ThreadId tid, OffT guest_state_offset,
                               SizeT size, Addr f )
{
   UWord p;

   // Having to do this is a bit nasty...
   if (f == (Addr)an_replace_malloc
       || f == (Addr)an_replace___builtin_new
       || f == (Addr)an_replace___builtin_vec_new
       || f == (Addr)an_replace_calloc
       || f == (Addr)an_replace_memalign
       || f == (Addr)an_replace_realloc)
   {
      // We remembered the last added segment;  make sure it's the right one.
      /* What's going on: at this point, the scheduler has just called
         'f' -- one of our malloc replacement functions -- and it has
         returned.  The return value has been written to the guest
         state of thread 'tid', offset 'guest_state_offset' length
         'size'.  We need to look at that return value and set the
         shadow return value accordingly.  The shadow return value
         required is handed to us "under the counter" through the
         global variable 'last_seg_added'.  This is all very ugly, not
         to mention, non-thread-safe should V ever become
         multithreaded. */
      /* assert the place where the return value is is a legit int reg */
      tl_assert(is_integer_guest_reg(guest_state_offset, size));
      /* Now we need to look at the returned value, to see whether the
         malloc succeeded or not. */
      p = get_guest_intreg(tid, 0/*non-shadow*/, guest_state_offset, size);
      if ((UInt)NULL == p) {
         // if alloc failed, eg. realloc on bogus pointer
         put_guest_intreg(tid, 1/*first-shadow*/,
                          guest_state_offset, size, (UWord)UNKNOWN );
      } else {
         // alloc didn't fail.  Check we have the correct segment.
         tl_assert(p == Seg__a(last_seg_added));
         put_guest_intreg(tid, 1/*first-shadow*/,
                          guest_state_offset, size, (UWord)last_seg_added );
      }
   } 
   else if (f == (Addr)an_replace_free
            || f == (Addr)an_replace___builtin_delete
            || f == (Addr)an_replace___builtin_vec_delete
   //            || f == (Addr)VG_(cli_block_size)
            || f == (Addr)VG_(message))
   {
      // Probably best to set the (non-existent!) return value to
      // non-pointer.
      tl_assert(is_integer_guest_reg(guest_state_offset, size));
      put_guest_intreg(tid, 1/*first-shadow*/,
                       guest_state_offset, size, (UWord)UNKNOWN );
   }
   else {
      // Anything else, probably best to set return value to non-pointer.
      //VG_(set_thread_shadow_archreg)(tid, reg, (UInt)UNKNOWN);
      Char fbuf[100];
      VG_(printf)("f = %p\n", f);
      VG_(get_fnname)(f, fbuf, 100);
      VG_(printf)("name = %s\n", fbuf);
      VG_(tool_panic)("argh: clientcall");
   }
}



//zz /*--------------------------------------------------------------------*/
//zz /*--- Sanity checking                                              ---*/
//zz /*--------------------------------------------------------------------*/
//zz 
//zz /* Check that nobody has spuriously claimed that the first or last 16
//zz    pages (64 KB) of address space have become accessible.  Failure of
//zz    the following do not per se indicate an internal consistency
//zz    problem, but they are so likely to that we really want to know
//zz    about it if so. */
//zz Bool an_replace_cheap_sanity_check) ( void )
//zz {
//zz    if (IS_DISTINGUISHED_SM(primary_map[0])
//zz        /* kludge: kernel drops a page up at top of address range for
//zz           magic "optimized syscalls", so we can no longer check the
//zz           highest page */
//zz        /* && IS_DISTINGUISHED_SM(primary_map[65535]) */
//zz       )
//zz       return True;
//zz    else
//zz       return False;
//zz }
//zz 
//zz Bool SK_(expensive_sanity_check) ( void )
//zz {
//zz    Int i;
//zz 
//zz    /* Make sure nobody changed the distinguished secondary. */
//zz    for (i = 0; i < SEC_MAP_WORDS; i++)
//zz       if (distinguished_secondary_map.vseg[i] != UNKNOWN)
//zz          return False;
//zz 
//zz    return True;
//zz }

/*--------------------------------------------------------------------*/
/*--- System calls                                                 ---*/
/*--------------------------------------------------------------------*/

static void pre_syscall ( ThreadId tid, UInt syscallno )
{
//zz #if 0
//zz    UInt mmap_flags;
//zz    if (90 == syscallno) {
//zz       // mmap: get contents of ebx to find args block, then extract 'flags'
//zz       UInt* arg_block = (UInt*)VG_(get_thread_archreg)(tid, R_EBX);
//zz       VG_(printf)("arg_block = %p\n", arg_block);
//zz       mmap_flags = arg_block[3];
//zz       VG_(printf)("flags = %d\n", mmap_flags);
//zz 
//zz    } else if (192 == syscallno) {
//zz       // mmap2: get flags from 4th register arg
//zz       mmap_flags = VG_(get_thread_archreg)(tid, R_ESI);
//zz 
//zz    } else {
//zz       goto out;
//zz    }
//zz 
//zz    if (0 != (mmap_flags & VKI_MAP_FIXED)) {
//zz       VG_(skin_panic)("can't handle MAP_FIXED flag to mmap()");
//zz    }
//zz 
//zz out:
//zz #endif
//zz    return NULL;
}

static void post_syscall ( ThreadId tid, UInt syscallno, SysRes res )
{
   switch (syscallno) {

      /* For the most part, syscalls don't return pointers.  So set
         the return shadow to unknown. */
      case __NR_access:
#     if defined(__NR_arch_prctl)
      case __NR_arch_prctl:
#     endif
      case __NR_close:
      case __NR_exit_group:
      case __NR_getcwd:
      case __NR_getrlimit:
      case __NR_fadvise64:
      case __NR_fstat:
#     if defined(__NR_fstat64)
      case __NR_fstat64:
#     endif
      case __NR_mprotect:
      case __NR_munmap: // die_mem_munmap already called, segment removed
      case __NR_open:
      case __NR_read:
      case __NR_set_robust_list:
      case __NR_set_thread_area:
      case __NR_set_tid_address:
      case __NR_rt_sigaction:
      case __NR_rt_sigprocmask:
      case __NR_stat:
#     if defined(__NR_stat64)
      case __NR_stat64:
#     endif
#     if defined(__NR_ugetrlimit)
      case __NR_ugetrlimit:
#     endif
      case __NR_uname:
      case __NR_write:
         VG_(set_syscall_return_shadows)( tid, (UWord)UNKNOWN, 0 );
         break;

//zz    // These ones don't return a pointer, so don't do anything -- already set
//zz    // the segment to UNKNOWN in post_reg_write_default().
//zz    //   1:           // exit              (never seen by skin)
//      case 2 ... 16:    // read ... lchown
//zz    //   17:          // break             (unimplemented)
//zz    //   18:          // oldstat           (obsolete)
//zz    case 19:          // lseek
//zz    case 20 ... 25:   // getpid ... stime
//zz    case 26:          // ptrace -- might return pointers, but not ones that
//zz                      // deserve their own segment.
//zz    case 27:          // alarm
//zz   //   28:          // oldfstat          (obsolete)
//zz    case 29 ... 30:   // pause ... utime
//zz    //   31:          // stty              (unimplemented)
//zz    //   32:          // gtty              (unimplemented)
//      case 33 ... 34:   // access ... nice
//zz    //   35:          // ftime             (obsolete and/or unimplemented)
//zz    case 36 ... 43:   // sync ... times
//zz    //   44:          // prof              (unimplemented)
//zz    //   45:          // brk               (below)
//zz    case 46 ... 47:   // setgid ... getgid
//zz    //   48:          // signal            (never seen by skin)
//zz    case 49 ... 50:   // geteuid ... getegid
//zz    //   51:          // acct              (never seen by skin?  not handled by core)
//zz    case 52:          // umount2
//zz    //   53:          // lock              (unimplemented)
//zz    case 54:          // ioctl -- assuming no pointers returned
//zz    case 55:          // fcntl
//zz    //   56:          // mpx               (unimplemented)
//zz    case 57:          // setpgid
//zz    //   58:          // ulimit            (unimplemented?  not handled by core)
//zz    //   59:          // oldolduname       (obsolete)
//zz    case 60 ... 67:   // sigaction
//zz    //   68:          // sgetmask          (?? not handled by core)
//zz    //   69:          // ssetmask          (?? not handled by core)
//zz    case 70 ... 71:   // setreuid ... setguid
//zz    //   72:          // sigsuspend        (handled by core? never seen by skins)
//      case 73 ... 83:   // sigpending ... symlink
//zz    //   84:          // oldlstat          (obsolete)
//zz    case 85:          // readlink
//zz    //   86:          // uselib            (not in core)
//zz    //   87:          // swapon            (not in core)
//zz    //   88:          // reboot            (not in core)
//zz    //   89:          // readdir           (not in core)
//zz    //   90:          // mmap              (below)
//      case 91:          // munmap;  die_mem_munmap already called, segment removed
//zz    case 92 ... 97:   // truncate ... setpriority
//zz    //   98:          // profil            (not in core)
//zz    case 99 ... 101:  // statfs ... ioperm
//zz    case 102:         // socketcall -- assuming no pointers returned
//zz    case 103 ... 108: // syslog ... fstat
//zz    //   109:         // olduname          (obsolete)
//zz    case 110 ... 111: // iopl ... vhangup
//zz    //   112:         // idle              (not in core, only used by process 0)
//zz    //   113:         // vm86old           (not in core)
//zz    case 114:         // wait4
//zz    //   115:         // swapoff           (not in core)
//zz    case 116:         // sysinfo
//zz    case 117:         // ipc -- assuming no pointers returned
//zz    case 118:         // fsync
//zz    //   119:         // sigreturn         (not in core, & shouldn't be called)
//      case 120:         // clone             (not handled by core)
//zz    //   121:         // setdomainname     (not in core)
//      case 122 ... 126: // uname ... sigprocmask
//zz    //   127:         // create_module     (not in core)
//zz    case 128:         // init_module
//zz    //   129:         // delete_module     (not in core)
//zz    //   130:         // get_kernel_syms   (not in core)
//zz    case 131 ... 133: // quotactl ... fchdir
//zz    //   134:         // bdflush           (not in core)
//zz    //   135:         // sysfs             (not in core)
//zz    case 136:         // personality 
//zz    //   137:         // afs_syscall       (not in core)
//zz    case 138 ... 160: // setfsuid ... sched_get_priority_min
//zz    //   161:         // rr_get_interval
//zz    case 162:         // nanosleep
//zz    //   163:         // mremap            (below)
//zz    case 164 ... 165: // setresuid ... getresuid
//zz    //   166:         // vm86              (not in core)
//zz    //   167:         // query_module      (not in core) 
//zz    case 168:         // poll
//zz    //   169:         // nfsservctl        (not in core)
//zz    case 170 ... 172: // setresgid ... prctl
//zz    //   173:         // rt_sigreturn      (not in core)
//      case 174 ... 177: // rt_sigaction ... rt_sigtimedwait
//zz    //   178:         // rt_sigqueueinfo   (not in core)
//      case 179 ... 191: // rt_sigsuspend ... ugetrlimit
//zz    //   192:         // mmap              (below)
//      case 193 ... 216: // truncate64 ... setfsgid32
//zz    //   217:         // pivot_root        (not in core)
//zz    //   218:         // mincore           (not in core)
//zz    case 219 ... 221: // madvise ... fcntl64
//zz    //   222:         // ???               (no such syscall?)
//zz    //   223:         // security          (not in core)
//zz    //   224:         // gettid            (not in core)
//zz    //   225:         // readahead         (not in core)
//zz    case 226 ... 237: // setxattr ... fremovexattr
//zz    //   238:         // tkill             (not in core)
//zz    case 239:         // sendfile64
//zz    //   240:         // futex             (not in core)
//zz    //   241:         // sched_setaffinity (not in core)
//zz    //   242:         // sched_getaffinity (not in core)
//      case 243:         // set_thread_area   (not in core)
//zz    //   244:         // get_thread_area   (not in core)
//zz    //   245:         // io_setup          (not in core)
//zz    //   246:         // io_destroy        (not in core)
//zz    //   247:         // io_getevents      (not in core)
//zz    //   248:         // io_submit         (not in core)
//zz    //   249:         // io_cancel         (not in core)
//      case 250:         // fadvise64
//zz    //   251:         // free_hugepages    (not in core)
//      case 252:         // exit_group
//zz    case 253:         // lookup_dcookie
//zz    //   254:         // sys_epoll_create  (?)
//zz    //   255:         // sys_epoll_ctl     (?)
//zz    //   256:         // sys_epoll_wait    (?)
//zz    //   257:         // remap_file_pages  (?)
//      case 258:         // set_tid_address   (?)

//   case 311: // set_robust_list
//        break;

   // With brk(), result (of kernel syscall, not glibc wrapper) is a heap
   // pointer.  Make the shadow UNKNOWN.
   case __NR_brk:
      VG_(set_syscall_return_shadows)( tid, (UWord)UNKNOWN, 0 );
      break;

   // With mmap, new_mem_mmap() has already been called and added the
   // segment (we did it there because we had the result address and size
   // handy).  So just set the return value shadow.
//zz    case 90:    // mmap
   case __NR_mmap:
#  if defined(__NR_mmap2)
   case __NR_mmap2:
#  endif
      if (res.isError) {
         // mmap() had an error, return value is a small negative integer
         VG_(set_syscall_return_shadows)( tid, (UWord)NONPTR, 0 );
      } else {
         // We remembered the last added segment; make sure it's the
         // right one.
         #if 0
         sk_assert(res == last_seg_added->left);
         VG_(set_return_from_syscall_shadow)( tid, (UInt)last_seg_added );
         #endif
         VG_(set_syscall_return_shadows)( tid, (UWord)UNKNOWN, 0 );
      }
      break;
//zz    case 163:
//zz       VG_(skin_panic)("can't handle mremap, sorry");
//zz       break;

   default:
      VG_(printf)("syscallno == %d\n", syscallno);
      VG_(tool_panic)("unhandled syscall");
   }
}

/*--------------------------------------------------------------------*/
/*--- Functions called from generated code                         ---*/
/*--------------------------------------------------------------------*/

static void checkSeg ( Seg vseg ) {
   tl_assert(vseg == UNKNOWN || vseg == NONPTR || vseg == BOTTOM
             || Seg__plausible(vseg) );
}

// XXX: could be more sophisticated -- actually track the lowest/highest
// valid address used by the program, and then return False for anything
// below that (using a suitable safety margin).  Also, nothing above
// 0xc0000000 is valid [unless you've changed that in your kernel]
static __inline__ Bool looks_like_a_pointer(Addr a)
{
   if (sizeof(UWord) == 4) {
      return (a > 0x01000000UL && a < 0xFF000000UL);
   } else {
      return (a > 0x01000000UL && a < 0xFF00000000000000UL);
   }
}

static __inline__ VG_REGPARM(1)
Seg nonptr_or_unknown(UWord x)
{
   return ( looks_like_a_pointer(x) ? UNKNOWN : NONPTR );
}

//zz static __attribute__((regparm(1)))
//zz void print_BB_entry(UInt bb)
//zz {
//zz    VG_(printf)("%u =\n", bb);
//zz }

// This function is called *a lot*; inlining it sped up Konqueror by 20%.
static __inline__
void check_load_or_store(Bool is_write, Addr m, UInt sz, Seg mptr_vseg)
{
   if (UNKNOWN == mptr_vseg) {
      // do nothing

   } else if (BOTTOM == mptr_vseg) {
      // do nothing

   } else if (NONPTR == mptr_vseg) {
      record_loadstore_error( m, sz, mptr_vseg, is_write );

   } else {
      // check all segment ranges in the circle
      // if none match, warn about 1st seg
      // else,          check matching one isn't freed
      Bool is_ok = False;
      Seg  curr  = mptr_vseg;
      Addr mhi;

      // Accesses partly outside range are an error, unless it's an aligned
      // word-sized read, and --partial-loads-ok=yes.  This is to cope with
      // gcc's/glibc's habits of doing word-sized accesses that read past
      // the ends of arrays/strings.
      if (!is_write && sz == sizeof(UWord)
          && clo_partial_loads_ok && SHMEM_IS_WORD_ALIGNED(m)) {
         mhi = m;
      } else {
         mhi = m+sz-1;
      }

      #if 0
      while (True) {
         lo  = curr->data;
         lim = lo + curr->size;
         if (m >= lo && mlim <= lim) { is_ok = True; break; }
         curr = curr->links;
         if (curr == mptr_vseg)      {               break; }
      }
      #else
      // This version doesn't do the link-segment chasing
      if (0) VG_(printf)("calling seg_ci %p %p %p\n", curr,m,mhi);
      is_ok = Seg__containsI(curr, m, mhi);
      #endif

      // If it's an overrun/underrun of a freed block, don't give both
      // warnings, since the first one mentions that the block has been
      // freed.
      if ( ! is_ok || Seg__is_freed(curr) )
         record_loadstore_error( m, sz, mptr_vseg, is_write );
   }
}

// ------------------ Load handlers ------------------ //

/* On 32 bit targets, we will use:
      check_load1 check_load2 check_load4W
   On 64 bit targets, we will use:
      check_load1 check_load2 check_load4 check_load8W
*/

// This handles 64 bit loads on 64 bit targets.  It must
// not be called on 32 bit targets.
// return m.vseg
static VG_REGPARM(2)
Seg check_load8W(Addr m, Seg mptr_vseg)
{
   Seg vseg;
   tl_assert(sizeof(UWord) == 8); /* DO NOT REMOVE */
checkSeg(mptr_vseg);
   check_load_or_store(/*is_write*/False, m, 8, mptr_vseg);
   if (VG_IS_8_ALIGNED(m)) {
      vseg = get_mem_vseg(m);
   } else {
      vseg = nonptr_or_unknown( *(ULong*)m );
   }
   return vseg;
}

// This handles 32 bit loads on 32 bit targets.  It must
// not be called on 64 bit targets.
// return m.vseg
static VG_REGPARM(2)
Seg check_load4W(Addr m, Seg mptr_vseg)
{
   Seg vseg;
   tl_assert(sizeof(UWord) == 4); /* DO NOT REMOVE */
checkSeg(mptr_vseg);
   check_load_or_store(/*is_write*/False, m, 4, mptr_vseg);
   if (VG_IS_4_ALIGNED(m)) {
      vseg = get_mem_vseg(m);
   } else {
      vseg = nonptr_or_unknown( *(UInt*)m );
   }
   return vseg;
}

// This handles 32 bit loads on 64 bit targets.  It must not
// be called on 32 bit targets.
static VG_REGPARM(2)
void check_load4(Addr m, Seg mptr_vseg)
{
   tl_assert(sizeof(UWord) == 8); /* DO NOT REMOVE */
checkSeg(mptr_vseg);
   check_load_or_store(/*is_write*/False, m, 4, mptr_vseg);
}

// Used for both 32 bit and 64 bit targets.
static VG_REGPARM(2)
void check_load2(Addr m, Seg mptr_vseg)
{
checkSeg(mptr_vseg);
   check_load_or_store(/*is_write*/False, m, 2, mptr_vseg);
}

// Used for both 32 bit and 64 bit targets.
static VG_REGPARM(2)
void check_load1(Addr m, Seg mptr_vseg)
{
checkSeg(mptr_vseg);
   check_load_or_store(/*is_write*/False, m, 1, mptr_vseg);
}

// ------------------ Store handlers ------------------ //

/* On 32 bit targets, we will use:
      check_store1 check_store2 check_store4W
   On 64 bit targets, we will use:
      check_store1 check_store2 check_store4 check_store8W
*/

// This handles 64 bit stores on 64 bit targets.  It must
// not be called on 32 bit targets.
static VG_REGPARM(3)
void check_store8W(Addr m, Seg mptr_vseg, UWord t, Seg t_vseg)
{
   tl_assert(sizeof(UWord) == 8); /* DO NOT REMOVE */
checkSeg(t_vseg);
checkSeg(mptr_vseg);
   check_load_or_store(/*is_write*/True, m, 8, mptr_vseg);
   // Actually *do* the STORE here
   *(ULong*)m = t;
   if (VG_IS_8_ALIGNED(m)) {
      set_mem_vseg( m, t_vseg );
   } else {
      // straddling two words
      m = VG_ROUNDDN(m,8);
      set_mem_vseg( m, nonptr_or_unknown( *(ULong*)m ) );
      m += 8;
      set_mem_vseg( m, nonptr_or_unknown( *(ULong*)m ) );
   }
}

// This handles 32 bit stores on 32 bit targets.  It must
// not be called on 64 bit targets.
static VG_REGPARM(3)
void check_store4W(Addr m, Seg mptr_vseg, UWord t, Seg t_vseg)
{
   tl_assert(sizeof(UWord) == 4); /* DO NOT REMOVE */
checkSeg(t_vseg);
checkSeg(mptr_vseg);
   check_load_or_store(/*is_write*/True, m, 4, mptr_vseg);
   // Actually *do* the STORE here
   *(UInt*)m = t;
   if (VG_IS_4_ALIGNED(m)) {
      set_mem_vseg( m, t_vseg );
   } else {
      // straddling two words
      m = VG_ROUNDDN(m,4);
      set_mem_vseg( m, nonptr_or_unknown( *(UInt*)m ) );
      m += 4;
      set_mem_vseg( m, nonptr_or_unknown( *(UInt*)m ) );
   }
}

// This handles 32 bit stores on 64 bit targets.  It must not
// be called on 32 bit targets.
static VG_REGPARM(3)
void check_store4(Addr m, Seg mptr_vseg, UWord t)
{
   tl_assert(sizeof(UWord) == 8); /* DO NOT REMOVE */
checkSeg(mptr_vseg);
   check_load_or_store(/*is_write*/True, m, 4, mptr_vseg);
   // Actually *do* the STORE here  (Nb: cast must be to 4-byte type!)
   *(UInt*)m = t;
   if (0 == (m & 4)) {
      // within one word.  This happens if the address ends in 
      // 000, 001, 010, 011.  If it ends in 100, 101, 110, 111
      // then it overlaps two adjacent 64 bit words.
      m = VG_ROUNDDN(m,8);
      set_mem_vseg( m, nonptr_or_unknown( *(ULong*)m ) );
   } else {
      // straddling two words
      m = VG_ROUNDDN(m,8);
      set_mem_vseg( m, nonptr_or_unknown( *(ULong*)m ) );
      m += 8;
      set_mem_vseg( m, nonptr_or_unknown( *(ULong*)m ) );
   }
}

// Used for both 32 bit and 64 bit targets.
static VG_REGPARM(3)
void check_store2(Addr m, Seg mptr_vseg, UWord t)
{
checkSeg(mptr_vseg);
   check_load_or_store(/*is_write*/True, m, 2, mptr_vseg);
   // Actually *do* the STORE here  (Nb: cast must be to 2-byte type!)
   *(UShort*)m = t;
   if (sizeof(UWord) == 4) {
      /* 32-bit host */
     if (3 != (m & 3)) {
         // within one word
         m = VG_ROUNDDN(m,4);
         set_mem_vseg( m, nonptr_or_unknown( *(UInt*)m ) );
      } else {
         // straddling two words
         m = VG_ROUNDDN(m,4);
         set_mem_vseg( m, nonptr_or_unknown( *(UInt*)m ) );
         m += 4;
         set_mem_vseg( m, nonptr_or_unknown( *(UInt*)m ) );
      }
   } else {
      /* 64-bit host */
      if (7 != (m & 7)) {
         // within one word
         m = VG_ROUNDDN(m,8);
         set_mem_vseg( m, nonptr_or_unknown( *(ULong*)m ) );
      } else {
         // straddling two words
         m = VG_ROUNDDN(m,8);
         set_mem_vseg( m, nonptr_or_unknown( *(ULong*)m ) );
         m += 8;
         set_mem_vseg( m, nonptr_or_unknown( *(ULong*)m ) );
      }
   }
}

// Used for both 32 bit and 64 bit targets.
static VG_REGPARM(3)
void check_store1(Addr m, Seg mptr_vseg, UWord t)
{
checkSeg(mptr_vseg);
   check_load_or_store(/*is_write*/True, m, 1, mptr_vseg);
   // Actually *do* the STORE here  (Nb: cast must be to 1-byte type!)
   *(UChar*)m = t;
   if (sizeof(UWord) == 4) {
      /* 32-bit host */
      m = VG_ROUNDDN(m,4);
      set_mem_vseg( m, nonptr_or_unknown( *(UInt*)m ) );
   } else {
      /* 64-bit host */
      m = VG_ROUNDDN(m,8);
      set_mem_vseg( m, nonptr_or_unknown( *(ULong*)m ) );
   }
}

//zz 
//zz #define CHECK_FPU_R(N)           \
//zz static __attribute__((regparm(2)))       \
//zz void check_fpu_r##N(Addr m, Seg mptr_vseg)  \
//zz {                                              \
//zz    VGP_PUSHCC(VgpCheckFpuR);                              \
//zz    check_load_or_store(/*is_write*/False, m, N, mptr_vseg);  \
//zz    VGP_POPCC(VgpCheckFpuR);                                     \
//zz }
//zz CHECK_FPU_R(2);
//zz CHECK_FPU_R(4);
//zz CHECK_FPU_R(8);
//zz CHECK_FPU_R(10);
//zz CHECK_FPU_R(16);
//zz CHECK_FPU_R(28);
//zz CHECK_FPU_R(108);
//zz 
//zz 
//zz #define CHECK_FPU_W(N)           \
//zz static __attribute__((regparm(2)))       \
//zz void check_fpu_w##N(Addr m, Seg mptr_vseg)  \
//zz {                                              \
//zz    VGP_PUSHCC(VgpCheckFpuW);                             \
//zz    check_load_or_store(/*is_write*/True, m, N, mptr_vseg);  \
//zz    set_mem_nonptr( m, N );                                     \
//zz    VGP_POPCC(VgpCheckFpuW);                                       \
//zz }
//zz CHECK_FPU_W(2);
//zz CHECK_FPU_W(4);
//zz CHECK_FPU_W(8);
//zz CHECK_FPU_W(10);
//zz CHECK_FPU_W(16);
//zz CHECK_FPU_W(28);
//zz CHECK_FPU_W(108);


// Nb: if the result is BOTTOM, return immedately -- don't let BOTTOM
//     be changed to NONPTR by a range check on the result.
#define BINOP(bt, nn, nu, np, un, uu, up, pn, pu, pp) \
   if (BOTTOM == seg1 || BOTTOM == seg2) { bt;                   \
   } else if (NONPTR == seg1)  { if      (NONPTR == seg2)  { nn; }  \
                                 else if (UNKNOWN == seg2) { nu; }    \
                                 else                      { np; }    \
   } else if (UNKNOWN == seg1) { if      (NONPTR == seg2)  { un; }    \
                                 else if (UNKNOWN == seg2) { uu; }    \
                                 else                      { up; }    \
   } else                      { if      (NONPTR == seg2)  { pn; }    \
                                 else if (UNKNOWN == seg2) { pu; }    \
                                 else                      { pp; }    \
   }

#define BINERROR(opname)                    \
   record_arith_error(seg1, seg2, opname);  \
   out = NONPTR

//zz // Extra arg for result of various ops
//zz static UInt do_op___result;

// -------------
//  + | n  ?  p
// -------------
//  n | n  ?  p
//  ? | ?  ?  ?
//  p | p  ?  e   (all results become n if they look like a non-pointer)
// -------------
static Seg do_addW_result(Seg seg1, Seg seg2, UWord result, HChar* opname)
{
checkSeg(seg1);
checkSeg(seg2);
   Seg out;
   BINOP(
      return BOTTOM,
      out = NONPTR,  out = UNKNOWN, out = seg2,
      out = UNKNOWN, out = UNKNOWN, out = UNKNOWN,
      out = seg1,    out = UNKNOWN,       BINERROR(opname)
   );
   return ( looks_like_a_pointer(result) ? out : NONPTR );
}

static VG_REGPARM(3) Seg do_addW(Seg seg1, Seg seg2, UWord result)
{
   Seg out;
checkSeg(seg1);
checkSeg(seg2);
   out = do_addW_result(seg1, seg2, result, "Add32/Add64");
checkSeg(out);
// VG_(printf)("do_add4: returning %p\n", out);
   return out;
}

// -------------
//  - | n  ?  p      (Nb: operation is seg1 - seg2)
// -------------
//  n | n  ?  n+     (+) happens a lot due to "cmp", but result should never
//  ? | ?  ?  n/B        be used, so give 'n'
//  p | p  p? n*/B   (*) and possibly link the segments
// -------------
static VG_REGPARM(3) Seg do_subW(Seg seg1, Seg seg2, UWord result)
{
checkSeg(seg1);
checkSeg(seg2);
   Seg out;
   // Nb: when returning BOTTOM, don't let it go through the range-check;
   //     a segment linking offset can easily look like a nonptr.
   BINOP(
      return BOTTOM,
      out = NONPTR,  out = UNKNOWN,    out = NONPTR,
      out = UNKNOWN, out = UNKNOWN,    return BOTTOM,
      out = seg1,    out = seg1/*??*/, return BOTTOM
   );
   #if 0
         // This is for the p-p segment-linking case
         Seg end2 = seg2;
         while (end2->links != seg2) end2 = end2->links;
         end2->links = seg1->links;
         seg1->links = seg2;
         return NONPTR;
   #endif
   return ( looks_like_a_pointer(result) ? out : NONPTR );
}

//zz // -------------
//zz // +-.| n  ?  p   (Nb: not very certain about these ones)
//zz // -------------
//zz //  n | n  ?  e
//zz //  ? | ?  ?  e
//zz //  p | p  e  e
//zz // -------------
//zz static __inline__
//zz Seg do_adcsbb4(Seg seg1, Seg seg2, Opcode opc)
//zz {
//zz    Seg out;
//zz 
//zz    VGP_PUSHCC(VgpDoAdcSbb);
//zz    BINOP(
//zz       VGP_POPCC(VgpDoAdcSbb); return BOTTOM,
//zz       out = NONPTR,  out = UNKNOWN, BINERROR(opc),
//zz       out = UNKNOWN, out = UNKNOWN, BINERROR(opc),
//zz       out = seg1,    BINERROR(opc), BINERROR(opc)
//zz    );
//zz    VGP_POPCC(VgpDoAdcSbb);
//zz    return ( looks_like_a_pointer(do_op___result) ? out : NONPTR );
//zz }
//zz 
//zz static __attribute__((regparm(2)))
//zz Seg do_adc4(Seg seg1, Seg seg2)
//zz {
//zz    return do_adcsbb4(seg1, seg2, ADC);
//zz }
//zz 
//zz static __attribute__((regparm(2)))
//zz Seg do_sbb4(Seg seg1, Seg seg2)
//zz {
//zz    return do_adcsbb4(seg1, seg2, SBB);
//zz }

// -------------
//  & | n  ?  p
// -------------
//  n | n  ?  p
//  ? | ?  ?  ?
//  p | p  ?  *  (*) if p1==p2 then p else e
// -------------

static VG_REGPARM(3) Seg do_andW(Seg seg1, Seg seg2, 
                                 UWord result, UWord args_diff)
{
   Seg out;
   if (0 == args_diff) {
      // p1==p2
      out = seg1;
   } else {
      BINOP(
         return BOTTOM,
         out = NONPTR,  out = UNKNOWN, out = seg2,
         out = UNKNOWN, out = UNKNOWN, out = UNKNOWN,
         out = seg1,    out = UNKNOWN,       BINERROR("And32/And64")
      );
   }
   out = ( looks_like_a_pointer(result) ? out : NONPTR );
   return out;
}

// -------------
// `|`| n  ?  p
// -------------
//  n | n  ?  p
//  ? | ?  ?  ?
//  p | p  ?  e
// -------------
static VG_REGPARM(3) Seg do_orW(Seg seg1, Seg seg2, UWord result)
{
   Seg out;
   BINOP(
      return BOTTOM,
      out = NONPTR,  out = UNKNOWN, out = seg2,
      out = UNKNOWN, out = UNKNOWN, out = UNKNOWN,
      out = seg1,    out = UNKNOWN,       BINERROR("Or32/Or64")
   );
   out = ( looks_like_a_pointer(result) ? out : NONPTR );
   return out;
}

//zz // -------------
//zz // L1 | n  ?  p
//zz // -------------
//zz //  n | n  ?  p
//zz //  ? | ?  ?  p
//zz // -------------
//zz static Seg do_lea(Bool k_looks_like_a_ptr, Seg seg1)
//zz {
//zz    Seg out;
//zz 
//zz    if (BOTTOM == seg1) return BOTTOM;
//zz 
//zz    if (k_looks_like_a_ptr && NONPTR == seg1)
//zz       out = UNKNOWN;      // ?(n)
//zz    else
//zz       out = seg1;         // n(n), n(?), n(p), ?(?), ?(p)
//zz 
//zz    return ( looks_like_a_pointer(do_op___result) ? out : NONPTR );
//zz }
//zz 
//zz static __attribute__((regparm(1)))
//zz Seg do_lea1_unknown(Seg seg1)
//zz {
//zz    Seg out;
//zz 
//zz    VGP_PUSHCC(VgpDoLea1);
//zz    out = do_lea( /*k_looks_like_a_ptr*/True, seg1 );
//zz    VGP_POPCC (VgpDoLea1);
//zz    return out;
//zz }
//zz 
//zz static UInt do_lea2___k;
//zz 
//zz static __attribute__((regparm(2)))
//zz Seg do_lea2_1(Seg seg1, Seg seg2)
//zz {
//zz    Seg out;
//zz 
//zz    // a) Combine seg1 and seg2 as for ADD, giving a result.
//zz    // b) Combine that result with k as for LEA1.
//zz    VGP_PUSHCC(VgpDoLea2);
//zz    out = do_lea( looks_like_a_pointer(do_lea2___k),
//zz                  do_add4_result(seg1, seg2, LEA2) );
//zz    VGP_POPCC (VgpDoLea2);
//zz    return out;
//zz }
//zz 
//zz static __attribute__((regparm(2)))
//zz Seg do_lea2_n(Seg seg1, Seg seg2)
//zz {
//zz    VGP_PUSHCC(VgpDoLea2);
//zz    VGP_POPCC (VgpDoLea2);
//zz 
//zz    if (is_known_segment(seg2)) {
//zz       // Could do with AsmError
//zz       VG_(message)(Vg_UserMsg,
//zz                    "\nScaling known pointer by value > 1 in lea instruction");
//zz    }
//zz    return do_lea(looks_like_a_pointer(do_lea2___k), seg1);
//zz }
//zz 
//zz // -------------
//zz //  - | n  ?  p
//zz // -------------
//zz //    | n  n  n
//zz // -------------
//zz static __attribute__((regparm(2)))
//zz Seg do_neg4(Seg seg1, UInt result)
//zz {
//zz    if (BOTTOM == seg1) return BOTTOM;
//zz 
//zz    return NONPTR;
//zz }

// -------------
//  ~ | n  ?  p
// -------------
//    | n  n  n
// -------------
static VG_REGPARM(2) Seg do_notW(Seg seg1, UWord result)
{
checkSeg(seg1);
   if (BOTTOM == seg1) return BOTTOM;
   return NONPTR;
}

// Pointers are rarely multiplied, but sometimes legitimately, eg. as hash
// function inputs.  But two pointers args --> error.
// Pretend it always returns a nonptr.  Maybe improve later.
static VG_REGPARM(2) Seg do_mulW(Seg seg1, Seg seg2)
{
checkSeg(seg1);
checkSeg(seg2);
   if (is_known_segment(seg1) && is_known_segment(seg2))
      record_arith_error(seg1, seg2, "Mul32/Mul64");
   return NONPTR;
}

 
//zz static __attribute__((regparm(2)))
//zz void check_imul4(Seg seg1, Seg seg2)
//zz {
//zz    if (is_known_segment(seg1) && is_known_segment(seg2))
//zz       record_arith_error(0xA5, 0xA5, seg1, seg2, -VGOFF_(helper_imul_32_64));
//zz }
//zz 
//zz // seg1 / seg2: div_64_32 can take a pointer for its 2nd arg (seg1).
//zz static __attribute__((regparm(2)))
//zz void check_div4(Seg seg1, Seg seg2)
//zz {
//zz    if (is_known_segment(seg2))
//zz       record_arith_error(0xA5, 0xA5, seg1, seg2, -VGOFF_(helper_div_64_32));
//zz }
//zz 
//zz // seg1 / seg2: idiv_64_32 can take a pointer for its 2nd or 3rd arg
//zz // (because the 3rd arg, ie. the high-word, will be derived from the
//zz // low-word with a 'sar' in order to get the sign-bit correct).  But we only
//zz // check the lo-word.
//zz static __attribute__((regparm(2)))
//zz void check_idiv4(Seg seg1, Seg seg2)
//zz {
//zz    if (is_known_segment(seg2))
//zz       record_arith_error(0xA5, 0xA5, seg1, seg2, -VGOFF_(helper_idiv_64_32));
//zz }
//zz 
//zz 
//zz /*--------------------------------------------------------------------*/
//zz /*--- Instrumentation                                              ---*/
//zz /*--------------------------------------------------------------------*/
//zz 
//zz static void set_nonptr_or_unknown(UCodeBlock* cb, UInt t)
//zz {
//zz    sk_assert(0 == t % 2);     // check number is even, ie. a normal tempreg
//zz    VG_(ccall_R_R)(cb, (Addr)nonptr_or_unknown,
//zz                   t,            // t      (reg)
//zz                   SHADOW(t),    // t.vseg (reg)(retval)
//zz                   1);
//zz }
//zz 
//zz static void set_shadowreg(UCodeBlock* cb, UInt q, Seg seg)
//zz {
//zz    sk_assert(1 == q % 2);     // check number is odd, ie. a shadow tempreg
//zz    VG_(lit_to_reg)(cb, (UInt)seg, q);
//zz }
//zz 
//zz static void set_nonptr(UCodeBlock* cb, UInt q)
//zz {
//zz    set_shadowreg(cb, q, NONPTR);
//zz }
//zz 
//zz __attribute__((unused))
//zz static void record_binary_arith_args(UCodeBlock* cb, UInt arg1, UInt arg2)
//zz {
//zz    VG_(reg_to_globvar)(cb, arg1, &actual_arg1);
//zz    VG_(reg_to_globvar)(cb, arg2, &actual_arg2);
//zz }



/* Carries around state during Annelid instrumentation. */
typedef
   struct {
      /* MODIFIED: the superblock being constructed.  IRStmts are
         added. */
      IRSB* bb;
      Bool  trace;

      /* MODIFIED: a table [0 .. #temps_in_original_bb-1] which maps
         original temps to their current their current shadow temp.
         Initially all entries are IRTemp_INVALID.  Entries are added
         lazily since many original temps are not used due to
         optimisation prior to instrumentation.  Note that only
         integer temps of the guest word size are shadowed, since it
         is impossible (or meaningless) to hold a pointer in any other
         type of temp. */
      IRTemp* tmpMap;
      Int     n_originalTmps; /* for range checking */

      /* READONLY: the host word type.  Needed for constructing
         arguments of type 'HWord' to be passed to helper functions.
         Ity_I32 or Ity_I64 only. */
      IRType hWordTy;

      /* READONLY: the guest word type, Ity_I32 or Ity_I64 only. */
      IRType gWordTy;

      /* READONLY: the guest state size, so we can generate shadow
         offsets correctly. */
      Int guest_state_sizeB;
   }
   ANEnv;

/* SHADOW TMP MANAGEMENT.  Shadow tmps are allocated lazily (on
   demand), as they are encountered.  This is for two reasons.

   (1) (less important reason): Many original tmps are unused due to
   initial IR optimisation, and we do not want to spaces in tables
   tracking them.

   Shadow IRTemps are therefore allocated on demand.  mce.tmpMap is a
   table indexed [0 .. n_types-1], which gives the current shadow for
   each original tmp, or INVALID_IRTEMP if none is so far assigned.
   It is necessary to support making multiple assignments to a shadow
   -- specifically, after testing a shadow for definedness, it needs
   to be made defined.  But IR's SSA property disallows this.

   (2) (more important reason): Therefore, when a shadow needs to get
   a new value, a new temporary is created, the value is assigned to
   that, and the tmpMap is updated to reflect the new binding.

   A corollary is that if the tmpMap maps a given tmp to
   IRTemp_INVALID and we are hoping to read that shadow tmp, it means
   there's a read-before-write error in the original tmps.  The IR
   sanity checker should catch all such anomalies, however.
*/

/* Find the tmp currently shadowing the given original tmp.  If none
   so far exists, allocate one.  */
static IRTemp findShadowTmp ( ANEnv* ane, IRTemp orig )
{
   tl_assert(orig < ane->n_originalTmps);
   tl_assert(ane->bb->tyenv->types[orig] == ane->gWordTy);
   if (ane->tmpMap[orig] == IRTemp_INVALID) {
      tl_assert(0);
      ane->tmpMap[orig]
         = newIRTemp(ane->bb->tyenv, ane->gWordTy);
   }
   return ane->tmpMap[orig];
}

/* Allocate a new shadow for the given original tmp.  This means any
   previous shadow is abandoned.  This is needed because it is
   necessary to give a new value to a shadow once it has been tested
   for undefinedness, but unfortunately IR's SSA property disallows
   this.  Instead we must abandon the old shadow, allocate a new one
   and use that instead. */
__attribute__((noinline))
static IRTemp newShadowTmp ( ANEnv* ane, IRTemp orig )
{
   tl_assert(orig < ane->n_originalTmps);
   tl_assert(ane->bb->tyenv->types[orig] == ane->gWordTy);
   ane->tmpMap[orig]
      = newIRTemp(ane->bb->tyenv, ane->gWordTy);
   return ane->tmpMap[orig];
}


/*------------------------------------------------------------*/
/*--- IRAtoms -- a subset of IRExprs                       ---*/
/*------------------------------------------------------------*/

/* An atom is either an IRExpr_Const or an IRExpr_Tmp, as defined by
   isIRAtom() in libvex_ir.h.  Because this instrumenter expects flat
   input, most of this code deals in atoms.  Usefully, a value atom
   always has a V-value which is also an atom: constants are shadowed
   by constants, and temps are shadowed by the corresponding shadow
   temporary. */

typedef  IRExpr  IRAtom;

/* (used for sanity checks only): is this an atom which looks
   like it's from original code? */
static Bool isOriginalAtom ( ANEnv* ane, IRAtom* a1 )
{
   if (a1->tag == Iex_Const)
      return True;
   if (a1->tag == Iex_RdTmp && a1->Iex.RdTmp.tmp < ane->n_originalTmps)
      return True;
   return False;
}

/* (used for sanity checks only): is this an atom which looks
   like it's from shadow code? */
static Bool isShadowAtom ( ANEnv* ane, IRAtom* a1 )
{
   if (a1->tag == Iex_Const)
      return True;
   if (a1->tag == Iex_RdTmp && a1->Iex.RdTmp.tmp >= ane->n_originalTmps)
      return True;
   return False;
}

/* (used for sanity checks only): check that both args are atoms and
   are identically-kinded. */
static Bool sameKindedAtoms ( IRAtom* a1, IRAtom* a2 )
{
   if (a1->tag == Iex_RdTmp && a2->tag == Iex_RdTmp)
      return True;
   if (a1->tag == Iex_Const && a2->tag == Iex_Const)
      return True;
   return False;
}


/*------------------------------------------------------------*/
/*--- Constructing IR fragments                            ---*/
/*------------------------------------------------------------*/

/* add stmt to a bb */
static inline void stmt ( HChar cat, ANEnv* ane, IRStmt* st ) {
   if (ane->trace) {
      VG_(printf)("  %c: ", cat);
      ppIRStmt(st);
      VG_(printf)("\n");
   }
   addStmtToIRSB(ane->bb, st);
}

/* assign value to tmp */
static inline
void assign ( HChar cat, ANEnv* ane, IRTemp tmp, IRExpr* expr ) {
   stmt(cat, ane, IRStmt_WrTmp(tmp,expr));
}

/* build various kinds of expressions */
#define binop(_op, _arg1, _arg2) IRExpr_Binop((_op),(_arg1),(_arg2))
#define unop(_op, _arg)          IRExpr_Unop((_op),(_arg))
#define mkU8(_n)                 IRExpr_Const(IRConst_U8(_n))
#define mkU16(_n)                IRExpr_Const(IRConst_U16(_n))
#define mkU32(_n)                IRExpr_Const(IRConst_U32(_n))
#define mkU64(_n)                IRExpr_Const(IRConst_U64(_n))
#define mkV128(_n)               IRExpr_Const(IRConst_V128(_n))
#define mkexpr(_tmp)             IRExpr_RdTmp((_tmp))

/* Bind the given expression to a new temporary, and return the
   temporary.  This effectively converts an arbitrary expression into
   an atom.

   'ty' is the type of 'e' and hence the type that the new temporary
   needs to be.  But passing it is redundant, since we can deduce the
   type merely by inspecting 'e'.  So at least that fact to assert
   that the two types agree. */
static IRAtom* assignNew ( HChar cat, ANEnv* ane, IRType ty, IRExpr* e ) {
   IRTemp t;
   IRType tyE = typeOfIRExpr(ane->bb->tyenv, e);
   tl_assert(tyE == ty); /* so 'ty' is redundant (!) */
   t = newIRTemp(ane->bb->tyenv, ty);
   assign(cat, ane, t, e);
   return mkexpr(t);
}



//-----------------------------------------------------------------------
// Approach taken for range-checking for NONPTR/UNKNOWN-ness as follows.
//
// Range check (NONPTR/seg): 
// - after modifying a word-sized value in/into a TempReg:
//    - {ADD, SUB, ADC, SBB, AND, OR, XOR, LEA, LEA2, NEG, NOT}L
//    - BSWAP
// 
// Range check (NONPTR/UNKNOWN):
// - when introducing a new word-sized value into a TempReg:
//    - MOVL l, t2
//
// - when copying a word-sized value which lacks a corresponding segment
//   into a TempReg:
//    - straddled LDL
//
// - when a sub-word of a word (or two) is updated:
//    - SHROTL
//    - {ADD, SUB, ADC, SBB, AND, OR, XOR, SHROT, NEG, NOT}[WB]
//    - PUT[WB]
//    - straddled   STL (2 range checks)
//    - straddled   STW (2 range checks)
//    - unstraddled STW
//    - STB
//    
// Just copy:
// - when copying word-sized values:
//    - MOVL t1, t2 (--optimise=no only)
//    - CMOV
//    - GETL, PUTL
//    - unstraddled LDL, unstraddled STL
//
// - when barely changing
//    - INC[LWB]/DEC[LWB]
// 
// Set to NONPTR:
// - after copying a sub-word value into a TempReg:
//    - MOV[WB] l, t2
//    - GET[WB]
//    - unstraddled LDW
//    - straddled   LDW
//    - LDB
//    - POP[WB]
//
// - after copying an obvious non-ptr into a TempReg:
//    - GETF
//    - CC2VAL
//    - POPL
//
// - after copying an obvious non-ptr into a memory word:
//    - FPU_W
// 
// Do nothing:
// - LOCK, INCEIP
// - WIDEN[WB]
// - JMP, JIFZ
// - CALLM_[SE], PUSHL, CALLM, CLEAR
// - FPU, FPU_R (and similar MMX/SSE ones)
//




/* Call h_fn (name h_nm) with the given arg, and return a new IRTemp
   holding the result.  The arg must be a word-typed atom.  Callee
   must be a VG_REGPARM(1) function. */
__attribute__((noinline))
static IRTemp gen_dirty_W_W ( ANEnv* ane, void* h_fn, HChar* h_nm, 
                              IRExpr* a1 )
{
   IRTemp   res;
   IRDirty* di;
   tl_assert(isIRAtom(a1));
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a1) == ane->gWordTy);
   res = newIRTemp(ane->bb->tyenv, ane->gWordTy);
   di = unsafeIRDirty_1_N( res, 1/*regparms*/,
                           h_nm, VG_(fnptr_to_fnentry)( h_fn ),
                           mkIRExprVec_1( a1 ) );
   stmt( 'I', ane, IRStmt_Dirty(di) );
   return res;
}

/* Two-arg version of gen_dirty_W_W.  Callee must be a VG_REGPARM(2)
   function.*/
static IRTemp gen_dirty_W_WW ( ANEnv* ane, void* h_fn, HChar* h_nm, 
                               IRExpr* a1, IRExpr* a2 )
{
   IRTemp   res;
   IRDirty* di;
   tl_assert(isIRAtom(a1));
   tl_assert(isIRAtom(a2));
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a1) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a2) == ane->gWordTy);
   res = newIRTemp(ane->bb->tyenv, ane->gWordTy);
   di = unsafeIRDirty_1_N( res, 2/*regparms*/,
                           h_nm, VG_(fnptr_to_fnentry)( h_fn ),
                           mkIRExprVec_2( a1, a2 ) );
   stmt( 'I', ane, IRStmt_Dirty(di) );
   return res;
}

/* Three-arg version of gen_dirty_W_W.  Callee must be a VG_REGPARM(3)
   function.*/
static IRTemp gen_dirty_W_WWW ( ANEnv* ane, void* h_fn, HChar* h_nm, 
                                IRExpr* a1, IRExpr* a2, IRExpr* a3 )
{
   IRTemp   res;
   IRDirty* di;
   tl_assert(isIRAtom(a1));
   tl_assert(isIRAtom(a2));
   tl_assert(isIRAtom(a3));
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a1) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a2) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a3) == ane->gWordTy);
   res = newIRTemp(ane->bb->tyenv, ane->gWordTy);
   di = unsafeIRDirty_1_N( res, 3/*regparms*/,
                           h_nm, VG_(fnptr_to_fnentry)( h_fn ),
                           mkIRExprVec_3( a1, a2, a3 ) );
   stmt( 'I', ane, IRStmt_Dirty(di) );
   return res;
}

/* Four-arg version of gen_dirty_W_W.  Callee must be a VG_REGPARM(3)
   function.*/
static IRTemp gen_dirty_W_WWWW ( ANEnv* ane, void* h_fn, HChar* h_nm, 
                                 IRExpr* a1, IRExpr* a2,
                                 IRExpr* a3, IRExpr* a4 )
{
   IRTemp   res;
   IRDirty* di;
   tl_assert(isIRAtom(a1));
   tl_assert(isIRAtom(a2));
   tl_assert(isIRAtom(a3));
   tl_assert(isIRAtom(a4));
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a1) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a2) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a3) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a4) == ane->gWordTy);
   res = newIRTemp(ane->bb->tyenv, ane->gWordTy);
   di = unsafeIRDirty_1_N( res, 3/*regparms*/,
                           h_nm, VG_(fnptr_to_fnentry)( h_fn ),
                           mkIRExprVec_4( a1, a2, a3, a4 ) );
   stmt( 'I', ane, IRStmt_Dirty(di) );
   return res;
}

/* Version of gen_dirty_W_WW with no return value.  Callee must be a
   VG_REGPARM(2) function.*/
static void gen_dirty_v_WW ( ANEnv* ane, void* h_fn, HChar* h_nm, 
                             IRExpr* a1, IRExpr* a2 )
{
   IRDirty* di;
   tl_assert(isIRAtom(a1));
   tl_assert(isIRAtom(a2));
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a1) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a2) == ane->gWordTy);
   di = unsafeIRDirty_0_N( 2/*regparms*/,
                           h_nm, VG_(fnptr_to_fnentry)( h_fn ),
                           mkIRExprVec_2( a1, a2 ) );
   stmt( 'I', ane, IRStmt_Dirty(di) );
}

/* Version of gen_dirty_W_WWW with no return value.  Callee must be a
   VG_REGPARM(3) function.*/
static void gen_dirty_v_WWW ( ANEnv* ane, void* h_fn, HChar* h_nm, 
                              IRExpr* a1, IRExpr* a2, IRExpr* a3 )
{
   IRDirty* di;
   tl_assert(isIRAtom(a1));
   tl_assert(isIRAtom(a2));
   tl_assert(isIRAtom(a3));
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a1) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a2) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a3) == ane->gWordTy);
   di = unsafeIRDirty_0_N( 3/*regparms*/,
                           h_nm, VG_(fnptr_to_fnentry)( h_fn ),
                           mkIRExprVec_3( a1, a2, a3 ) );
   stmt( 'I', ane, IRStmt_Dirty(di) );
}

/* Version of gen_dirty_v_WWW for 4 arguments.  Callee must be a
   VG_REGPARM(3) function.*/
static void gen_dirty_v_WWWW ( ANEnv* ane, void* h_fn, HChar* h_nm, 
                               IRExpr* a1, IRExpr* a2,
                               IRExpr* a3, IRExpr* a4 )
{
   IRDirty* di;
   tl_assert(isIRAtom(a1));
   tl_assert(isIRAtom(a2));
   tl_assert(isIRAtom(a3));
   tl_assert(isIRAtom(a4));
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a1) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a2) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a3) == ane->gWordTy);
   tl_assert(typeOfIRExpr(ane->bb->tyenv, a4) == ane->gWordTy);
   di = unsafeIRDirty_0_N( 3/*regparms*/,
                           h_nm, VG_(fnptr_to_fnentry)( h_fn ),
                           mkIRExprVec_4( a1, a2, a3, a4 ) );
   stmt( 'I', ane, IRStmt_Dirty(di) );
}

static IRAtom* uwiden_to_host_word ( ANEnv* ane, IRAtom* a )
{
   IRType a_ty = typeOfIRExpr(ane->bb->tyenv, a);
   tl_assert(isIRAtom(a));
   if (ane->hWordTy == Ity_I32) {
      switch (a_ty) {
         case Ity_I8:
            return assignNew( 'I', ane, Ity_I32, unop(Iop_8Uto32, a) );
         case Ity_I16:
            return assignNew( 'I', ane, Ity_I32, unop(Iop_16Uto32, a) );
         default:
            ppIRType(a_ty);
            tl_assert(0);
      }
   } else {
      tl_assert(ane->hWordTy == Ity_I64);
      switch (a_ty) {
         case Ity_I8:
            return assignNew( 'I', ane, Ity_I64, unop(Iop_8Uto64, a) );
         case Ity_I16:
            return assignNew( 'I', ane, Ity_I64, unop(Iop_16Uto64, a) );
         case Ity_I32:
            return assignNew( 'I', ane, Ity_I64, unop(Iop_32Uto64, a) );
         default:
            ppIRType(a_ty);
            tl_assert(0);
      }
   }
}

/* 'e' is a word-sized atom.  Call nonptr_or_unknown with it, bind the
   results to a new temporary, and return the temporary.  Note this
   takes an original expression but returns a shadow value. */
static IRTemp gen_call_nonptr_or_unknown_w ( ANEnv* ane, IRExpr* e )
{
   return gen_dirty_W_W( ane, &nonptr_or_unknown, 
                              "nonptr_or_unknown", e );
}


/* Generate the shadow value for an IRExpr which is an atom and
   guaranteed to be word-sized. */
static IRAtom* schemeEw_Atom ( ANEnv* ane, IRExpr* e )
{
   if (ane->gWordTy == Ity_I32) {
      if (e->tag == Iex_Const && e->Iex.Const.con->tag == Ico_U32) {
         IRTemp t;
         tl_assert(sizeof(UWord) == 4);
         t = gen_call_nonptr_or_unknown_w(ane, e);
         return mkexpr(t);
      }
      if (e->tag == Iex_RdTmp
          && typeOfIRExpr(ane->bb->tyenv, e) == Ity_I32) {
         return mkexpr( findShadowTmp(ane, e->Iex.RdTmp.tmp) );
      }
      /* there are no other word-sized atom cases */
   } else {
      if (e->tag == Iex_Const && e->Iex.Const.con->tag == Ico_U64) {
         IRTemp t;
         tl_assert(sizeof(UWord) == 8);
         t = gen_call_nonptr_or_unknown_w(ane, e);
         return mkexpr(t);
      }
      if (e->tag == Iex_RdTmp
          && typeOfIRExpr(ane->bb->tyenv, e) == Ity_I64) {
         return mkexpr( findShadowTmp(ane, e->Iex.RdTmp.tmp) );
      }
      /* there are no other word-sized atom cases */
   }
   ppIRExpr(e);
   tl_assert(0);
}


static
void instrument_arithop ( ANEnv* ane,
                          IRTemp dst, /* already holds result */
                          IRTemp dstv, /* generate an assignment to this */
                          IROp op,
                          /* original args, guaranteed to be atoms */
                          IRExpr* a1, IRExpr* a2, IRExpr* a3, IRExpr* a4 )
{
   HChar*  nm  = NULL;
   void*   fn  = NULL;
   IRExpr* a1v = NULL;
   IRExpr* a2v = NULL;
   IRExpr* a3v = NULL;
   IRExpr* a4v = NULL;
   IRTemp  res = IRTemp_INVALID;

   if (ane->gWordTy == Ity_I32) {

      tl_assert(ane->hWordTy == Ity_I32);
      switch (op) {

         /* For these cases, pass Segs for both arguments, and the
            result value. */
         case Iop_Add32: nm = "do_addW"; fn = &do_addW; goto ssr32;
         case Iop_Sub32: nm = "do_subW"; fn = &do_subW; goto ssr32;
         case Iop_Or32:  nm = "do_orW";  fn = &do_orW;  goto ssr32;
         ssr32:
            a1v = schemeEw_Atom( ane, a1 );
            a2v = schemeEw_Atom( ane, a2 );
            res = gen_dirty_W_WWW( ane, fn, nm, a1v, a2v, mkexpr(dst) );
            assign( 'I', ane, dstv, mkexpr(res) );
            break;

         /* In this case, pass Segs for both arguments, the result
            value, and the difference between the (original) values of
            the arguments. */
         case Iop_And32:
            nm = "do_andW"; fn = &do_andW;
            a1v = schemeEw_Atom( ane, a1 );
            a2v = schemeEw_Atom( ane, a2 );
            res = gen_dirty_W_WWWW( 
                     ane, fn, nm, a1v, a2v, mkexpr(dst),
                     assignNew( 'I', ane, Ity_I32,
                                binop(Iop_Sub32,a1,a2) ) );
            assign( 'I', ane, dstv, mkexpr(res) );
            break;

         /* Pass one shadow arg and the result to the helper. */
         case Iop_Not32: nm = "do_notW"; fn = &do_notW; goto vr32;
         vr32:
            a1v = schemeEw_Atom( ane, a1 );
            res = gen_dirty_W_WW( ane, fn, nm, a1v, mkexpr(dst) );
            assign( 'I', ane, dstv, mkexpr(res) );
            break;

         /* Pass two shadow args only to the helper. */
         case Iop_Mul32: nm = "do_mulW"; fn = &do_mulW; goto vv32;
         vv32:
            a1v = schemeEw_Atom( ane, a1 );
            a2v = schemeEw_Atom( ane, a2 );
            res = gen_dirty_W_WW( ane, fn, nm, a1v, a2v );
            assign( 'I', ane, dstv, mkexpr(res) );
            break;

         /* We don't really know what the result could be; test at run
            time. */
         case Iop_64HIto32: goto n_or_u_32;
         case Iop_64to32:   goto n_or_u_32;
         case Iop_Shl32:    goto n_or_u_32;
         case Iop_Sar32:    goto n_or_u_32;
         case Iop_Shr32:    goto n_or_u_32;
         case Iop_Xor32:    goto n_or_u_32;
         case Iop_16Uto32:  goto n_or_u_32;
         case Iop_16Sto32:  goto n_or_u_32;
         n_or_u_32:
            assign( 'I', ane, dstv,
                    mkexpr(
                       gen_call_nonptr_or_unknown_w( ane, 
                                                     mkexpr(dst) ) ) );
            break;

         /* Cases where it's very obvious that the result cannot be a
            pointer.  Hence declare directly that it's NONPTR; don't
            bother with the overhead of calling nonptr_or_unknown. */
         case Iop_1Uto32: goto n32;
         case Iop_8Uto32: goto n32;
         case Iop_8Sto32: goto n32;
         n32:
            assign( 'I', ane, dstv, mkU32( (UInt)NONPTR ));
            break;

         default:
            VG_(printf)("instrument_arithop(32-bit): unhandled: ");
            ppIROp(op);
            tl_assert(0);
      }

   } else {

      tl_assert(ane->gWordTy == Ity_I64);
      switch (op) {

         /* For these cases, pass Segs for both arguments, and the
            result value. */
         case Iop_Add64: nm = "do_addW"; fn = &do_addW; goto ssr64;
         case Iop_Sub64: nm = "do_subW"; fn = &do_subW; goto ssr64;
         case Iop_Or64:  nm = "do_orW";  fn = &do_orW;  goto ssr64;
         ssr64:
            a1v = schemeEw_Atom( ane, a1 );
            a2v = schemeEw_Atom( ane, a2 );
            res = gen_dirty_W_WWW( ane, fn, nm, a1v, a2v, mkexpr(dst) );
            assign( 'I', ane, dstv, mkexpr(res) );
            break;

         /* In this case, pass Segs for both arguments, the result
            value, and the difference between the (original) values of
            the arguments. */
         case Iop_And64:
            nm = "do_andW"; fn = &do_andW;
            a1v = schemeEw_Atom( ane, a1 );
            a2v = schemeEw_Atom( ane, a2 );
            res = gen_dirty_W_WWWW( 
                     ane, fn, nm, a1v, a2v, mkexpr(dst),
                     assignNew( 'I', ane, Ity_I64,
                                binop(Iop_Sub64,a1,a2) ) );
            assign( 'I', ane, dstv, mkexpr(res) );
            break;

         /* Pass one shadow arg and the result to the helper. */
         case Iop_Not64: nm = "do_notW"; fn = &do_notW; goto vr64;
         vr64:
            a1v = schemeEw_Atom( ane, a1 );
            res = gen_dirty_W_WW( ane, fn, nm, a1v, mkexpr(dst) );
            assign( 'I', ane, dstv, mkexpr(res) );
            break;

         /* Pass two shadow args only to the helper. */
         case Iop_Mul64: nm = "do_mulW"; fn = &do_mulW; goto vv64;
         vv64:
            a1v = schemeEw_Atom( ane, a1 );
            a2v = schemeEw_Atom( ane, a2 );
            res = gen_dirty_W_WW( ane, fn, nm, a1v, a2v );
            assign( 'I', ane, dstv, mkexpr(res) );
            break;

         /* We don't really know what the result could be; test at run
            time. */
         case Iop_32Uto64:   goto n_or_u_64;
         case Iop_32Sto64:   goto n_or_u_64;
         case Iop_Shl64:     goto n_or_u_64;
         case Iop_Sar64:     goto n_or_u_64;
         case Iop_Shr64:     goto n_or_u_64;
         case Iop_Xor64:     goto n_or_u_64;
         case Iop_128HIto64: goto n_or_u_64;
         case Iop_128to64:   goto n_or_u_64;
         case Iop_16Uto64:   goto n_or_u_64;
         case Iop_32HLto64:  goto n_or_u_64;
         case Iop_MullS32:   goto n_or_u_64;
         case Iop_MullU32:   goto n_or_u_64;
         n_or_u_64:
            assign( 'I', ane, dstv,
                    mkexpr(
                       gen_call_nonptr_or_unknown_w( ane, 
                                                     mkexpr(dst) ) ) );
            break;

         /* Cases where it's very obvious that the result cannot be a
            pointer.  Hence declare directly that it's NONPTR; don't
            bother with the overhead of calling nonptr_or_unknown. */
         case Iop_1Uto64:        goto n64;
         case Iop_8Uto64:        goto n64;
         case Iop_8Sto64:        goto n64;
         case Iop_DivModU64to32: goto n64;
         case Iop_DivModS64to32: goto n64;
         n64:
            assign( 'I', ane, dstv, mkU64( (UInt)NONPTR ));
            break;

         default:
            VG_(printf)("instrument_arithop(64-bit): unhandled: ");
            ppIROp(op);
            tl_assert(0);
      }
   }
}


/* iii describes zero or more non-exact integer register updates.  For
   each one, generate IR to get the containing register, apply
   nonptr_or_unknown to it, and write it back again. */
static void do_nonptr_or_unknown_for_III( ANEnv* ane, IntRegInfo* iii )
{
   Int i;
   tl_assert(iii && iii->n_offsets >= 0);
   for (i = 0; i < iii->n_offsets; i++) {
      IRAtom* a1 = assignNew( 'I', ane, ane->gWordTy, 
                              IRExpr_Get( iii->offsets[i], ane->gWordTy ));
      IRTemp a2 = gen_call_nonptr_or_unknown_w( ane, a1 );
      stmt( 'I', ane, IRStmt_Put( iii->offsets[i] 
                                     + ane->guest_state_sizeB,
                                  mkexpr(a2) ));
   }
}

/* Generate into 'ane', instrumentation for 'st'.  Also copy 'st'
   itself into 'ane' (the caller does not do so).  This is somewhat
   complex and relies heavily on the assumption that the incoming IR
   is in flat form.

   Generally speaking, the instrumentation is placed after the
   original statement, so that results computed by the original can be
   used in the instrumentation.  However, that isn't safe for memory
   references, since we need the instrumentation (hence bounds check
   and potential error message) to happen before the reference itself,
   as the latter could cause a fault. */
static void schemeS ( ANEnv* ane, IRStmt* st )
{
   tl_assert(st);
   tl_assert(isFlatIRStmt(st));

   switch (st->tag) {

      case Ist_Dirty: {
         Int i;
         IRDirty* di;
         stmt( 'C', ane, st );
         /* nasty.  assumes that (1) all helpers are unconditional,
            and (2) all outputs are non-ptr */
         di = st->Ist.Dirty.details;
         /* deal with the return tmp, if any */
         if (di->tmp != IRTemp_INVALID
             && typeOfIRTemp(ane->bb->tyenv, di->tmp) == ane->gWordTy) {
            /* di->tmp is shadowed.  Set it to NONPTR. */
            IRTemp dstv = newShadowTmp( ane, di->tmp );
            if (ane->gWordTy == Ity_I32) {
               assign( 'I', ane, dstv, mkU32( (UInt)NONPTR ));
            } else {
               assign( 'I', ane, dstv, mkU64( (ULong)NONPTR ));
            }
         }
         /* apply the nonptr_or_unknown technique to any parts of
            the guest state that happen to get written */
         for (i = 0; i < di->nFxState; i++) {
            IntRegInfo iii;
            tl_assert(di->fxState[i].fx != Ifx_None);
            if (di->fxState[i].fx == Ifx_Read)
               continue; /* this bit is only read -- not interesting */
            get_IntRegInfo( &iii, di->fxState[i].offset,
                                  di->fxState[i].size );
            tl_assert(iii.n_offsets >= -1 
                      && iii.n_offsets <= N_INTREGINFO_OFFSETS);
            /* Deal with 3 possible cases, same as with Ist_Put
               elsewhere in this function. */
            if (iii.n_offsets == -1) {
               /* case (1): exact write of an integer register. */
               IRAtom* a1
                  = assignNew( 'I', ane, ane->gWordTy, 
                               IRExpr_Get( iii.offsets[i], ane->gWordTy ));
               IRTemp a2 = gen_call_nonptr_or_unknown_w( ane, a1 );
               stmt( 'I', ane, IRStmt_Put( iii.offsets[i] 
                                              + ane->guest_state_sizeB,
                                           mkexpr(a2) ));
            } else {
               tl_assert(0); /* awaiting test case */
               /* when == 0: case (3): no instrumentation needed */
               /* when > 0: case (2) .. complex case.  Fish out the
                  stored value for the whole register, heave it
                  through nonptr_or_unknown, and use that as the new
                  shadow value. */
               tl_assert(iii.n_offsets >= 0 
                         && iii.n_offsets <= N_INTREGINFO_OFFSETS);
               do_nonptr_or_unknown_for_III( ane, &iii );
            }
         } /* for (i = 0; i < di->nFxState; i++) */
         /* punt on memory outputs */
         if (di->mFx != Ifx_None)
            goto unhandled;
         break;
      }

      case Ist_NoOp:
         break;

      /* nothing interesting in these; just copy them through */
      case Ist_AbiHint:
      case Ist_MBE:
      case Ist_Exit:
      case Ist_IMark:
         stmt( 'C', ane, st );
         break;

      case Ist_Put: {
         /* PUT(offset) = atom */
         /* 3 cases:
            1. It's a complete write of an integer register.  Get hold of
               'atom's shadow value and write it in the shadow state.
            2. It's a partial write of an integer register.  Let the write
               happen, then fish out the complete register value and see if,
               via range checking, consultation of tea leaves, etc, its
               shadow value can be upgraded to anything useful.
            3. It is none of the above.  Generate no instrumentation. */
         IntRegInfo iii;
         IRType     ty;
         stmt( 'C', ane, st );
         ty = typeOfIRExpr(ane->bb->tyenv, st->Ist.Put.data);
         get_IntRegInfo( &iii, st->Ist.Put.offset,
                         sizeofIRType(ty) );
         if (iii.n_offsets == -1) {
            /* case (1): exact write of an integer register. */
            tl_assert(ty == ane->gWordTy);
            stmt( 'I', ane,
                       IRStmt_Put( st->Ist.Put.offset
                                      + ane->guest_state_sizeB,
                                   schemeEw_Atom( ane, st->Ist.Put.data)) );
         } else {
            /* when == 0: case (3): no instrumentation needed */
            /* when > 0: case (2) .. complex case.  Fish out the
               stored value for the whole register, heave it through
               nonptr_or_unknown, and use that as the new shadow
               value. */
            tl_assert(iii.n_offsets >= 0 
                      && iii.n_offsets <= N_INTREGINFO_OFFSETS);
            do_nonptr_or_unknown_for_III( ane, &iii );
         }
         break;
      } /* case Ist_Put */

      case Ist_Store: {
         /* We have: STle(addr) = data
            if data is int-word sized, do
            check_store4(addr, addr#, data, data#)
            for all other stores
            check_store{1,2}(addr, addr#, data)

            The helper actually *does* the store, so that it can do
            the post-hoc ugly hack of inspecting and "improving" the
            shadow data after the store, in the case where it isn't an
            aligned word store.
         */
         IRExpr* data  = st->Ist.Store.data;
         IRExpr* addr  = st->Ist.Store.addr;
         IRType  d_ty  = typeOfIRExpr(ane->bb->tyenv, data);
         HChar*  h_nm  = NULL;
         void*   h_fn  = NULL;
         IRExpr* addrv = NULL;
         if (ane->gWordTy == Ity_I32) {
            /* 32 bit host/guest (cough, cough) */
            switch (d_ty) {
               case Ity_I32: h_fn = &check_store4W; 
                             h_nm = "check_store4W"; break;
               case Ity_I16: h_fn = &check_store2;
                             h_nm = "check_store2"; break;
               case Ity_I8:  h_fn = &check_store1;
                             h_nm = "check_store1"; break;
               default: tl_assert(0);
            }
            addrv = schemeEw_Atom( ane, addr );
            if (d_ty == Ity_I32) {
               IRExpr* datav = schemeEw_Atom( ane, data );
               gen_dirty_v_WWWW( ane, h_fn, h_nm, addr, addrv,
                                                  data, datav );
            } else {
               gen_dirty_v_WWW( ane, h_fn, h_nm, addr, addrv,
                                     uwiden_to_host_word( ane, data ));
            }
         } else {
            /* 64 bit host/guest (cough, cough) */
            switch (d_ty) {
               case Ity_I64: h_fn = &check_store8W; 
                             h_nm = "check_store8W"; break;
               case Ity_I32: h_fn = &check_store4; 
                             h_nm = "check_store4"; break;
               case Ity_I16: h_fn = &check_store2; 
                             h_nm = "check_store2"; break;
               case Ity_I8:  h_fn = &check_store1;
                             h_nm = "check_store1"; break;
               default: ppIRType(d_ty); tl_assert(0);
            }
            addrv = schemeEw_Atom( ane, addr );
            if (d_ty == Ity_I64) {
               IRExpr* datav = schemeEw_Atom( ane, data );
               gen_dirty_v_WWWW( ane, h_fn, h_nm, addr, addrv,
                                                  data, datav );
            } else {
               gen_dirty_v_WWW( ane, h_fn, h_nm, addr, addrv,
                                     uwiden_to_host_word( ane, data ));
            }
         }
         /* And don't copy the original, since the helper does the
            store.  Ick. */
         break;
      } /* case Ist_Store */

      case Ist_WrTmp: {
         /* This is the only place we have to deal with the full
            IRExpr range.  In all other places where an IRExpr could
            appear, we in fact only get an atom (Iex_RdTmp or
            Iex_Const). */
         IRExpr* e      = st->Ist.WrTmp.data;
         IRType  e_ty   = typeOfIRExpr( ane->bb->tyenv, e );
         Bool    isWord = e_ty == ane->gWordTy;
         IRTemp  dst    = st->Ist.WrTmp.tmp;
         IRTemp  dstv   = isWord ? newShadowTmp( ane, dst )
                                 : IRTemp_INVALID;

         switch (e->tag) {

            case Iex_Const: {
               stmt( 'C', ane, st );
               if (isWord)
                  assign( 'I', ane, dstv, schemeEw_Atom( ane, e ) );
               break;
            }

            case Iex_CCall: {
               stmt( 'C', ane, st );
               if (isWord)
                  assign( 'I', ane, dstv,
                          mkexpr( gen_call_nonptr_or_unknown_w( 
                                     ane, mkexpr(dst)))); 
               break;
            }

            case Iex_Mux0X: {
               /* Just steer the shadow values in the same way as the
                  originals. */
               stmt( 'C', ane, st );
               if (isWord)
                  assign( 'I', ane, dstv, 
                          IRExpr_Mux0X(
                             e->Iex.Mux0X.cond,
                             schemeEw_Atom( ane, e->Iex.Mux0X.expr0 ),
                             schemeEw_Atom( ane, e->Iex.Mux0X.exprX ) ));
               break;
            }

            case Iex_RdTmp: {
               stmt( 'C', ane, st );
               if (isWord)
                  assign( 'I', ane, dstv, schemeEw_Atom( ane, e ));
               break;
            }

            case Iex_Load: {
               IRExpr* addr  = e->Iex.Load.addr;
               HChar*  h_nm  = NULL;
               void*   h_fn  = NULL;
               IRExpr* addrv = NULL;
               if (ane->gWordTy == Ity_I32) {
                  /* 32 bit host/guest (cough, cough) */
                  switch (e_ty) {
                     case Ity_I32: h_fn = &check_load4W;
                                   h_nm = "check_load4W"; break;
                     case Ity_I16: h_fn = &check_load2;
                                   h_nm = "check_load2"; break;
                     case Ity_I8:  h_fn = &check_load1;
                                   h_nm = "check_load1"; break;
                     default: tl_assert(0);
                  }
                  addrv = schemeEw_Atom( ane, addr );
                  if (e_ty == Ity_I32) {
                     assign( 'I', ane, dstv, 
                              mkexpr( gen_dirty_W_WW( ane, h_fn, h_nm,
                                                           addr, addrv )) );
                  } else {
                     gen_dirty_v_WW( ane, h_fn, h_nm, addr, addrv );
                  }
               } else {
                  /* 64 bit host/guest (cough, cough) */
                  switch (e_ty) {
                     case Ity_I64: h_fn = &check_load8W;
                                   h_nm = "check_load8W"; break;
                     case Ity_I32: h_fn = &check_load4;
                                   h_nm = "check_load4"; break;
                     case Ity_I16: h_fn = &check_load2;
                                   h_nm = "check_load2"; break;
                     case Ity_I8:  h_fn = &check_load1;
                                   h_nm = "check_load1"; break;
                     default: ppIRType(e_ty); tl_assert(0);
                  }
                  addrv = schemeEw_Atom( ane, addr );
                  if (e_ty == Ity_I64) {
                     assign( 'I', ane, dstv, 
                              mkexpr( gen_dirty_W_WW( ane, h_fn, h_nm,
                                                           addr, addrv )) );
                  } else {
                     gen_dirty_v_WW( ane, h_fn, h_nm, addr, addrv );
                  }
               }
               /* copy the original -- must happen after the helper call */
               stmt( 'C', ane, st );
               break;
            }

            case Iex_Get: {
               stmt( 'C', ane, st );
               if (isWord) {
                  /* guest-word-typed tmp assignment, so it will have a
                     shadow tmp, and we must make an assignment to
                     that */
                  if (is_integer_guest_reg(e->Iex.Get.offset,
                                           sizeofIRType(e->Iex.Get.ty))) {
                     assign( 'I', ane, dstv,
                             IRExpr_Get( e->Iex.Get.offset 
                                            + ane->guest_state_sizeB,
                                         e->Iex.Get.ty) );
                  } else {
                     if (ane->hWordTy == Ity_I32) {
                        assign( 'I', ane, dstv, mkU32( (UInt)NONPTR ));
                     } else {
                        assign( 'I', ane, dstv, mkU64( (ULong)NONPTR ));
                     }
                  }
               } else {
                  /* tmp isn't guest-word-typed, so isn't shadowed, so
                     generate no instrumentation */
               }
               break;
            }

            case Iex_Unop: {
               stmt( 'C', ane, st );
               tl_assert(isIRAtom(e->Iex.Unop.arg));
               if (isWord)
                  instrument_arithop( ane, dst, dstv, e->Iex.Unop.op,
                                      e->Iex.Unop.arg,
                                      NULL, NULL, NULL );
               break;
            }

            case Iex_Binop: {
               stmt( 'C', ane, st );
               tl_assert(isIRAtom(e->Iex.Binop.arg1));
               tl_assert(isIRAtom(e->Iex.Binop.arg2));
               if (isWord)
                  instrument_arithop( ane, dst, dstv, e->Iex.Binop.op,
                                      e->Iex.Binop.arg1, e->Iex.Binop.arg2,
                                      NULL, NULL );
               break;
            }

            default:
               goto unhandled;
         } /* switch (e->tag) */

         break;

      } /* case Ist_WrTmp */

      default:
      unhandled:
         ppIRStmt(st);
         tl_assert(0);
   }
//zz    static UInt bb = 0;
//zz 
//zz    UCodeBlock* cb;
//zz    Int         i;
//zz    UInstr*     u;
//zz    Addr        helper;
//zz    UInt        callm_shadow_arg4v[3];
//zz    UInt        callm_arg4c = -1;
//zz 
//zz    cb = VG_(setup_UCodeBlock)(cb_in);
//zz 
//zz    // Print BB number upon execution
//zz    if (0)
//zz       VG_(ccall_L_0)(cb, (Addr)print_BB_entry, bb++, 1);
//zz 
//zz 
//zz    for (i = 0; i < VG_(get_num_instrs)(cb_in); i++) {
//zz       u = VG_(get_instr)(cb_in, i);
//zz 
//zz       //-- Start main switch -------------------------------------------
//zz       switch (u->opcode) {
//zz 
//zz       case NOP:
//zz          break;
//zz 
//zz       case LOCK:
//zz       case INCEIP:
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       //--- Value movers -----------------------------------------------
//zz       case MOV:
//zz          // a) Do MOV
//zz          VG_(copy_UInstr)(cb, u);
//zz          if (4 == u->size) {
//zz             if (Literal == u->tag1) {
//zz                // MOVL l, t2
//zz                // b) t2.vseg = NONPTR/UNKNOWN
//zz                set_shadowreg(cb, SHADOW(u->val2), nonptr_or_unknown(u->lit32));
//zz             } else {
//zz                // MOVL t1, t2 (only occurs if --optimise=no)
//zz                // b) t2.vseg = t1.vseg
//zz                uInstr2(cb, MOV, 4, TempReg, SHADOW(u->val1),
//zz                                    TempReg, SHADOW(u->val2));
//zz             }
//zz          } else {
//zz             // MOV[WB] l, t2
//zz             // b) t2.vseg = NONPTR
//zz             sk_assert(Literal == u->tag1);
//zz             set_nonptr(cb, SHADOW(u->val2));
//zz          }
//zz          break;
//zz 
//zz       case CMOV:
//zz          // CMOV t1, t2
//zz          // a) t2.vseg = t1.vseg, if condition holds
//zz          // b) Do CMOV
//zz          sk_assert(4 == u->size);
//zz          uInstr2(cb, CMOV, 4, TempReg, SHADOW(u->val1),
//zz                               TempReg, SHADOW(u->val2));
//zz          uCond(cb, u->cond);
//zz          uFlagsRWU(cb, u->flags_r, u->flags_w, FlagsEmpty);
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case GET:
//zz          // a) Do GET
//zz          VG_(copy_UInstr)(cb, u);
//zz          if (4 == u->size) {
//zz             // GETL r, t2
//zz             // b) t2.vseg = r.vseg
//zz             uInstr2(cb, GETV, 4, ArchReg, u->val1, TempReg, SHADOW(u->val2));
//zz          } else {
//zz             // GET[WB] r, t2
//zz             // b) t2.vseg = NONPTR
//zz             set_nonptr(cb, SHADOW(u->val2));
//zz          }
//zz          break;
//zz 
//zz       case PUT: {
//zz          // a) Do PUT
//zz          VG_(copy_UInstr)(cb, u);
//zz          if (4 == u->size) {
//zz             // PUTL t1, r
//zz             // b) r.vseg = t1.vseg
//zz             uInstr2(cb, PUTV, 4, TempReg, SHADOW(u->val1), ArchReg, u->val2);
//zz          } else {
//zz             // PUT[WB] t1, r
//zz             // b) r.vseg = NONPTR/UNKNOWN
//zz             //    (GET the result of the PUT[WB], look at the resulting
//zz             //     word, PUTV the shadow result.  Ugh.)
//zz             UInt t_res = newTemp(cb);
//zz             uInstr2(cb, GET,  4, ArchReg, u->val2, TempReg, t_res);
//zz             set_nonptr_or_unknown(cb, t_res);
//zz             uInstr2(cb, PUTV, 4, TempReg, SHADOW(t_res), ArchReg, u->val2);
//zz          }
//zz          break;
//zz       }
//zz 
//zz       case GETF:
//zz          // GETF t1
//zz          // a) t1.vseg = NONPTR
//zz          // b) Do GETF
//zz          set_nonptr(cb, SHADOW(u->val1));
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case PUTF:
//zz          // PUTF t1
//zz          // a) Do PUTF
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case LOAD:
//zz          // LD[LWB] m, t
//zz          if      (4 == u->size) helper = (Addr)check_load4;
//zz          else if (2 == u->size) helper = (Addr)check_load2;
//zz          else if (1 == u->size) helper = (Addr)check_load1;
//zz          else    VG_(skin_panic)("bad LOAD size");
//zz 
//zz          // a) Check segments match (in helper)
//zz          // b) t.vseg = m.vseg (helper returns m.vseg)
//zz          // c) Do LOAD (must come after segment check!)
//zz          VG_(ccall_RR_R)(cb, helper,
//zz                          u->val1,            // m              (reg)
//zz                          SHADOW(u->val1),    // m_ptr.vseg     (reg)
//zz                          SHADOW(u->val2),    // t.vseg (retval)(reg)
//zz                          2);
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case STORE:
//zz          if (4 == u->size) {
//zz             // Put t.vseg in globvar-arg
//zz             VG_(reg_to_globvar)(cb, SHADOW(u->val1),
//zz                                 (UInt*) & check_store4___t_vseg);
//zz             helper = (Addr)check_store4;
//zz          } 
//zz          else if (1 == u->size) helper = (Addr)check_store1;
//zz          else if (2 == u->size) helper = (Addr)check_store2;
//zz          else                   VG_(skin_panic)("bad size for STORE");
//zz             
//zz          // a) Check segments match
//zz          // b) Do STORE (after segment check; done by helper)
//zz          //
//zz          // STL t, m
//zz          // c) if aligned(m), m.vseg = t.vseg
//zz          //    else vseg of both words touched set to UNKNOWN
//zz          //
//zz          // ST[WB] t, m
//zz          // c) if non-straddling(m), m.vseg = NONPTR/UNKNOWN
//zz          //    else vseg of both words touched set to UNKNOWN
//zz          VG_(ccall_RRR_0)(cb, helper,
//zz                               u->val2,          // m          (reg)
//zz                               SHADOW(u->val2),  // m_ptr.vseg (reg)
//zz                               u->val1,          // t          (reg)
//zz                               3);
//zz          break;
//zz 
//zz       //--- Binary arithmetic ------------------------------------------
//zz       case ADD: helper = (Addr)do_add4;  goto do_add_sub;
//zz       case ADC: helper = (Addr)do_adc4;  goto do_add_sub;
//zz       case SUB: helper = (Addr)do_sub4;  goto do_add_sub;
//zz       case SBB: helper = (Addr)do_sbb4;  goto do_add_sub;
//zz        do_add_sub:
//zz          // a) Do OP (and record result in globvar)
//zz //record_binary_arith_args(cb, u->val2, t1);
//zz 
//zz          VG_(copy_UInstr)(cb, u);
//zz          if (4 == u->size) {
//zz             UInt t1;
//zz             if (Literal == u->tag1) {
//zz                // OPL l, t2
//zz                Seg segL = nonptr_or_unknown(u->lit32);
//zz                t1 = newTemp(cb);
//zz                VG_(lit_to_reg)(cb, u->lit32,   t1);
//zz                VG_(lit_to_reg)(cb, (UInt)segL, SHADOW(t1));
//zz             } else if (ArchReg == u->tag1) {
//zz                // OPL r, t2
//zz                t1 = newTemp(cb);
//zz                uInstr2(cb, GET,  4, ArchReg, u->val1, TempReg, t1);
//zz                uInstr2(cb, GETV, 4, ArchReg, u->val1, TempReg, SHADOW(t1));
//zz             } else {
//zz                // OPL t1, t2
//zz                sk_assert(TempReg == u->tag1);
//zz                t1 = u->val1;
//zz             }
//zz 
//zz             // Put result in globvar-arg
//zz             VG_(reg_to_globvar)(cb, u->val2, &do_op___result);
//zz             // b) Check args (if necessary;  ok to do after the OP itself)
//zz             // c) Update t.vseg
//zz             VG_(ccall_RR_R)(cb, helper,
//zz                             SHADOW(u->val2),    // t2.vseg (reg)
//zz                             SHADOW(t1),         // t1.vseg (reg)
//zz                             SHADOW(u->val2),    // t2.vseg (reg)(retval)
//zz                             2);
//zz          } else {
//zz             // OP[WB] x, t2
//zz             // b) t2.vseg = UKNOWN
//zz             set_nonptr_or_unknown(cb, u->val2);
//zz          }
//zz          break;
//zz 
//zz       case AND:
//zz          // a) Do AND
//zz          VG_(copy_UInstr)(cb, u);
//zz          if (4 == u->size) {
//zz             // ANDL t1, t2
//zz             // Find difference between t1 and t2 (to determine if they're equal)
//zz             // put in globvar-arg
//zz             UInt t_equal = newTemp(cb);
//zz             uInstr2(cb, MOV, 4, TempReg, u->val2, TempReg, t_equal);
//zz             uInstr2(cb, SUB, 4, TempReg, u->val1, TempReg, t_equal);
//zz             VG_(reg_to_globvar)(cb, t_equal, &do_and4___args_diff);
//zz             // Put result in globvar-arg
//zz             VG_(reg_to_globvar)(cb, u->val2, &do_op___result);
//zz             // b) Update t2.vseg
//zz             VG_(ccall_RR_R)(cb, (Addr)do_and4,
//zz                            SHADOW(u->val1),    // t1.vseg (reg)
//zz                            SHADOW(u->val2),    // t2.vseg (reg)
//zz                            SHADOW(u->val2),    // t2.vseg (reg)(retval)
//zz                            2);
//zz          } else {
//zz             // AND[WB] t1, t2
//zz             // b) t2.vseg = NONPTR/UNKNOWN
//zz             set_nonptr_or_unknown(cb, u->val2);
//zz          }
//zz          break;
//zz 
//zz       case OR:
//zz          // a) Do OR
//zz          VG_(copy_UInstr)(cb, u);
//zz          if (4 == u->size) {
//zz             // ORL t1, t2
//zz             // Put result in globvar-arg
//zz             VG_(reg_to_globvar)(cb, u->val2, &do_op___result);
//zz             // b) Update t2.vseg
//zz             VG_(ccall_RR_R)(cb, (Addr)do_or4,
//zz                            SHADOW(u->val1),    // t1.vseg (reg)
//zz                            SHADOW(u->val2),    // t2.vseg (reg)
//zz                            SHADOW(u->val2),    // t2.vseg (reg)(retval)
//zz                            2);
//zz          } else {
//zz             // OR[WB] t1, t2
//zz             // b) t2.vseg = NONPTR/UNKNOWN
//zz             set_nonptr_or_unknown(cb, u->val2);
//zz          }
//zz          break;
//zz 
//zz       // With XOR, the result is likely to be a nonptr, but could
//zz       // occasionally not be due to weird things like xor'ing a pointer with
//zz       // zero, or using the xor trick for swapping two variables.  So,
//zz       // simplest thing is to just look at the range.
//zz       case XOR:
//zz          // XOR[LWB] x, t2
//zz          // a) Do XOR
//zz          // b) t2.vseg = NONPTR/UNKNOWN
//zz          VG_(copy_UInstr)(cb, u);
//zz          set_nonptr_or_unknown(cb, u->val2);
//zz          break;
//zz 
//zz       // Nb: t1 is always 1-byte, and thus never a pointer.
//zz       case SHL: case SHR: case SAR:
//zz       case ROL: case ROR:
//zz          // SHROT x, t2
//zz          // a) Do SHROT
//zz          // b) t2.vseg = NONPTR/UNKNOWN
//zz          VG_(copy_UInstr)(cb, u);
//zz          set_nonptr_or_unknown(cb, u->val2);
//zz          break;
//zz 
//zz       // LEA2 t1,t2,t3:  t3 = lit32 + t1 + (t2 * extra4b)
//zz       // Used for pointer computations, and for normal arithmetic (eg.
//zz       // "lea (%r1,%r1,4),%r2" multiplies by 5).
//zz       case LEA2:
//zz          sk_assert(4 == u->size);
//zz          // a) Do LEA2
//zz          VG_(copy_UInstr)(cb, u);
//zz          if (1 == u->extra4b) {
//zz             // t3 = lit32 + t1 + (t2*1)  (t2 can be pointer)
//zz             helper = (Addr)do_lea2_1;
//zz          } else {
//zz             // t3 = lit32 + t1 + (t2*n)  (t2 cannot be pointer)
//zz             helper = (Addr)do_lea2_n;
//zz          }
//zz          // Put k in globvar-arg
//zz          VG_(lit_to_globvar)(cb, u->lit32, &do_lea2___k);
//zz          // Put result in globvar-arg
//zz          VG_(reg_to_globvar)(cb, u->val3,  &do_op___result);
//zz          // b) Check args
//zz          // c) Update t3.vseg
//zz          VG_(ccall_RR_R)(cb, helper,
//zz                          SHADOW(u->val1),    // t1.vseg (reg)
//zz                          SHADOW(u->val2),    // t2.vseg (reg)
//zz                          SHADOW(u->val3),    // t3.vseg (reg)(retval)
//zz                          2);
//zz          break;
//zz 
//zz       //--- Unary arithmetic -------------------------------------------
//zz       case NEG: helper = (Addr)do_neg4;  goto do_neg_not;
//zz       case NOT: helper = (Addr)do_not4;  goto do_neg_not;
//zz        do_neg_not:
//zz          // a) Do NEG/NOT
//zz          VG_(copy_UInstr)(cb, u);
//zz          if (4 == u->size) {
//zz             // NEGL/NOTL t1
//zz             // b) Check args (NEG only)
//zz             // c) Update t1.vseg
//zz             VG_(ccall_RR_R)(cb, helper,
//zz                             SHADOW(u->val1),    // t1.vseg (reg)
//zz                             u->val2,            // t2      (reg)
//zz                             SHADOW(u->val1),    // t1.vseg (reg)(retval)
//zz                             2);
//zz          } else {
//zz             // NEG[WB]/NOT[WB] t1
//zz             // b) Update t1.vseg
//zz             set_nonptr_or_unknown(cb, u->val2);
//zz          }
//zz          break;
//zz 
//zz       case INC:
//zz       case DEC:
//zz          // INC[LWB]/DEC[LWB] t1
//zz          // a) Do INC/DEC
//zz          // b) t1.vseg unchanged (do nothing)
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case LEA1:
//zz          sk_assert(4 == u->size);
//zz          // LEA1 k(t1), t2
//zz          // b) Do LEA1
//zz          VG_(copy_UInstr)(cb, u);
//zz          if ( ! looks_like_a_pointer(u->lit32) ) {
//zz             // a) simple, 99.9% case: k is a known nonptr, t2.vseg = t1.vseg
//zz             uInstr2(cb, MOV, 4, TempReg, SHADOW(u->val1),
//zz                                 TempReg, SHADOW(u->val2));
//zz          } else {
//zz             // Put result in globvar-arg (not strictly necessary, because we
//zz             // have a spare CCALL slot, but do it for consistency)
//zz             VG_(reg_to_globvar)(cb, u->val2,  &do_op___result);
//zz             // a) complicated, 0.1% case: k could be a pointer
//zz             VG_(ccall_R_R)(cb, (Addr)do_lea1_unknown,
//zz                            SHADOW(u->val1),    // t1.vseg (reg)
//zz                            SHADOW(u->val2),    // t2.vseg (reg)(retval)
//zz                            1);
//zz          }
//zz          break;
//zz 
//zz       //--- End of arithmetic ------------------------------------------
//zz 
//zz       case WIDEN:
//zz          // WIDEN t1
//zz          // a) Do WIDEN
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case CC2VAL:
//zz          // CC2VAL t1
//zz          // a) t1.vseg = NONPTR
//zz          // b) Do CC2VAL
//zz          set_nonptr(cb, SHADOW(u->val1));
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case BSWAP:
//zz          // BSWAP t1
//zz          // a) t1.vseg = UNKNOWN
//zz          // b) Do BSWAP
//zz          set_nonptr_or_unknown(cb, u->val1);
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case JIFZ:
//zz       case JMP:
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       //--- CALLM and related ------------------------------------------
//zz       // Basic form of CALLMs, as regexp:
//zz       //   CALLM_S (GET? PUSH)* CALLM (POP PUT? | CLEAR)* CALLM_E
//zz       //
//zz       // How we're doing things:
//zz       // - PUSH:  Args will be popped/cleared, so no need to set the shadows.
//zz       //          Check input depending on the callee.
//zz       // - CALLM: Do nothing, already checked inputs on PUSH.
//zz       // - POP:   Depending on the callee, set popped outputs as
//zz       //          pointer/non-pointer.  But all callees seem to produce
//zz       //          NONPTR outputs, so just set output arg so straight off.
//zz       case CALLM_S:
//zz          sk_assert(-1 == callm_arg4c);
//zz          callm_arg4c = 0;
//zz          break;
//zz 
//zz       case CALLM_E:
//zz          sk_assert(-1 != callm_arg4c);
//zz          callm_arg4c = -1;
//zz          break;
//zz 
//zz       case PUSH:
//zz          sk_assert(-1 != callm_arg4c);
//zz          if (4 == u->size) {
//zz             // PUSHL t1
//zz             callm_shadow_arg4v[ callm_arg4c++ ] = SHADOW(u->val1); 
//zz          }
//zz          // a) Do PUSH
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       // All the ops using helpers certainly don't return pointers, except
//zz       // possibly, sh[rl]dl.  I'm not sure about them, but make the result
//zz       // NONPTR anyway.
//zz       case POP:
//zz          // POP t1
//zz          // a) set t1.vseg == NONPTR
//zz          // b) Do POP
//zz          sk_assert(-1 != callm_arg4c);
//zz          set_nonptr(cb, SHADOW(u->val1));
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case CLEAR:
//zz          sk_assert(-1 != callm_arg4c);
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case CALLM:
//zz          if (!(0 <= callm_arg4c && callm_arg4c <= 4)) {
//zz             VG_(printf)("callm_arg4c = %d\n", callm_arg4c);
//zz             VG_(skin_panic)("bleh");
//zz          }
//zz 
//zz          #define V(name)   (VGOFF_(helper_##name) == u->val1)
//zz 
//zz          if (V(mul_32_64)) {
//zz             sk_assert(2 == callm_arg4c);
//zz             VG_(ccall_RR_0)(cb, (Addr)check_mul4,
//zz                             callm_shadow_arg4v[1], // arg2.vseg (reg)
//zz                             callm_shadow_arg4v[0], // arg1.vseg (reg)
//zz                             2);
//zz 
//zz          } else if (V(imul_32_64)) {
//zz             sk_assert(2 == callm_arg4c);
//zz             VG_(ccall_RR_0)(cb, (Addr)check_imul4,
//zz                             callm_shadow_arg4v[1], // arg2.vseg (reg)
//zz                             callm_shadow_arg4v[0], // arg1.vseg (reg)
//zz                             2);
//zz 
//zz          } else if (V(div_64_32)) {
//zz             sk_assert(3 == callm_arg4c);
//zz             VG_(ccall_RR_0)(cb, (Addr)check_div4,
//zz                             callm_shadow_arg4v[1], // arg2.vseg (reg)
//zz                             callm_shadow_arg4v[0], // arg1.vseg (reg)
//zz                             2);
//zz 
//zz          } else if (V(idiv_64_32)) {
//zz             sk_assert(3 == callm_arg4c);
//zz             VG_(ccall_RR_0)(cb, (Addr)check_idiv4,
//zz                             callm_shadow_arg4v[1], // arg2.vseg (reg)
//zz                             callm_shadow_arg4v[0], // arg1.vseg (reg)
//zz                             2);
//zz 
//zz          } else if (V(shldl) || V(shrdl)) {
//zz             sk_assert(2 == callm_arg4c);
//zz             // a) Do shrdl/shldl
//zz 
//zz          } else if (V(bsf) || V(bsr)) {
//zz             // Shouldn't take a pointer?  Don't bother checking.
//zz             sk_assert(2 == callm_arg4c);
//zz 
//zz          } else if (V(get_dirflag)) {
//zz             // This always has zero pushed as the arg.
//zz             sk_assert(1 == callm_arg4c);
//zz 
//zz          } else if (V(RDTSC))    { sk_assert(2 == callm_arg4c);
//zz          } else if (V(CPUID))    { sk_assert(4 == callm_arg4c);
//zz          } else if (V(SAHF))     { sk_assert(1 == callm_arg4c);
//zz          } else if (V(LAHF))     { sk_assert(1 == callm_arg4c);
//zz          } else if (V(fstsw_AX)) { sk_assert(1 == callm_arg4c);
//zz 
//zz          } else {
//zz             // Others don't take any word-sized args
//zz             //sk_assert(0 == callm_arg4c);
//zz             if (0 != callm_arg4c) {
//zz                VG_(printf)("callm_arg4c = %d, %d, %d\n", 
//zz                            callm_arg4c, VGOFF_(helper_RDTSC), u->val1);
//zz                sk_assert(1 == 0);
//zz             }
//zz          }
//zz 
//zz          #undef V
//zz 
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz       //--- End of CALLM and related -----------------------------------
//zz 
//zz       case FPU:
//zz       case MMX1: case MMX2: case MMX3:
//zz       case SSE3: case SSE4: case SSE5:
//zz          // a) Do FPU/MMX[123]/SSE[345]
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       // We check the load, but don't do any updating, because the read is
//zz       // into FPU/MMX/SSE regs which we don't shadow.
//zz       case FPU_R:
//zz       case MMX2_MemRd:
//zz          switch (u->size) {
//zz          case 2:   helper = (Addr)check_fpu_r2;   break;
//zz          case 4:   helper = (Addr)check_fpu_r4;   break;
//zz          case 8:   helper = (Addr)check_fpu_r8;   break;
//zz          case 10:  helper = (Addr)check_fpu_r10;  break;
//zz          case 16:  helper = (Addr)check_fpu_r16;  break;
//zz          case 28:  helper = (Addr)check_fpu_r28;  break;
//zz          case 108: helper = (Addr)check_fpu_r108; break;
//zz          default:  VG_(skin_panic)("bad FPU_R/MMX/whatever size");
//zz          }
//zz          // FPU_R, MMX2_MemRd
//zz          // a) Check segments match (in helper)
//zz          // b) Do FPU_R/MMX2_MemRd
//zz          VG_(ccall_RR_0)(cb, helper,
//zz                          u->val2,            // m              (reg)
//zz                          SHADOW(u->val2),    // m_ptr.vseg     (reg)
//zz                          2);
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz 
//zz       case FPU_W: {
//zz       case MMX2_MemWr:
//zz          switch (u->size) {
//zz          case 2:   helper = (Addr)check_fpu_w2;   break;
//zz          case 4:   helper = (Addr)check_fpu_w4;   break;
//zz          case 8:   helper = (Addr)check_fpu_w8;   break;
//zz          case 10:  helper = (Addr)check_fpu_w10;  break;
//zz          case 16:  helper = (Addr)check_fpu_w16;  break;
//zz          case 28:  helper = (Addr)check_fpu_w28;  break;
//zz          case 108: helper = (Addr)check_fpu_w108; break;
//zz          default:  VG_(skin_panic)("bad FPU_W/MMX/whatever size");
//zz          }
//zz          // FPU_W, MMX2_MemWr
//zz          // a) Check segments match (in helper)
//zz          // b) m.vseg = NONPTR, for each touched memory word
//zz          // c) Do FPU_W/MMX2_MemWr
//zz          VG_(ccall_RR_0)(cb, helper,
//zz                          u->val2,            // m              (reg)
//zz                          SHADOW(u->val2),    // m_ptr.vseg     (reg)
//zz                          2);
//zz          VG_(copy_UInstr)(cb, u);
//zz          break;
//zz       }
//zz 
//zz       default:
//zz          VG_(pp_UInstr)(0, u);
//zz          VG_(skin_panic)("Redux: unhandled instruction");
//zz       }
//zz       //-- End main switch ---------------------------------------------
//zz    }
//zz 
//zz    VG_(free_UCodeBlock)(cb_in);
//zz 
//zz    // Optimisations
//zz //   {
//zz //      Int* live_range_ends = VG_(find_live_range_ends)(cb);
//zz //      VG_(redundant_move_elimination)(cb, live_range_ends);
//zz //      VG_(free_live_range_ends)(live_range_ends);
//zz //   }
//zz 
//zz    return cb;
}


static
IRSB* an_instrument ( VgCallbackClosure* closure,
                      IRSB* sbIn,
                      VexGuestLayout* layout,
                      VexGuestExtents* vge,
                      IRType gWordTy, IRType hWordTy )
{
   Bool  verboze = 0||False;
   Int   i /*, j*/;
   ANEnv ane;

   if (0) { /* See comment-ref below KLUDGE01. */
     /* FIXME: race! */
     static Bool init_kludge_done = False;
     if (!init_kludge_done) {
       init_kludge_done = True;
       init_shadow_registers(0);
     }
   }

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   /* Check we're not completely nuts */
   tl_assert(sizeof(UWord)  == sizeof(void*));
   tl_assert(sizeof(Word)   == sizeof(void*));
   tl_assert(sizeof(Addr)   == sizeof(void*));
   tl_assert(sizeof(ULong)  == 8);
   tl_assert(sizeof(Long)   == 8);
   tl_assert(sizeof(Addr64) == 8);
   tl_assert(sizeof(UInt)   == 4);
   tl_assert(sizeof(Int)    == 4);

   /* Set up the running environment.  Only .bb is modified as we go
      along. */
   ane.bb                = deepCopyIRSBExceptStmts(sbIn);
   ane.trace             = verboze;
   ane.n_originalTmps    = sbIn->tyenv->types_used;
   ane.hWordTy           = hWordTy;
   ane.gWordTy           = gWordTy;
   ane.guest_state_sizeB = layout->total_sizeB;
   ane.tmpMap            = LibVEX_Alloc(ane.n_originalTmps * sizeof(IRTemp));
   for (i = 0; i < ane.n_originalTmps; i++)
      ane.tmpMap[i] = IRTemp_INVALID;

   /* Stay sane.  These two should agree! */
   tl_assert(layout->total_sizeB == MC_SIZEOF_GUEST_STATE);

   /* Copy verbatim any IR preamble preceding the first IMark */

   i = 0;
   while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
      IRStmt* st = sbIn->stmts[i];
      tl_assert(st);
      tl_assert(isFlatIRStmt(st));
      stmt( 'C', &ane, sbIn->stmts[i] );
      i++;
   }

   /* Nasty problem.  IR optimisation of the pre-instrumented IR may
      cause the IR following the preamble to contain references to IR
      temporaries defined in the preamble.  Because the preamble isn't
      instrumented, these temporaries don't have any shadows.
      Nevertheless uses of them following the preamble will cause
      memcheck to generate references to their shadows.  End effect is
      to cause IR sanity check failures, due to references to
      non-existent shadows.  This is only evident for the complex
      preambles used for function wrapping on TOC-afflicted platforms
      (ppc64-linux, ppc32-aix5, ppc64-aix5).

      The following loop therefore scans the preamble looking for
      assignments to temporaries.  For each one found it creates an
      assignment to the corresponding shadow temp, marking it as
      'defined'.  This is the same resulting IR as if the main
      instrumentation loop before had been applied to the statement
      'tmp = CONSTANT'.
   */
#if 0
   // FIXME: this isn't exactly right; only needs to generate shadows
   // for guest-word-typed temps
   for (j = 0; j < i; j++) {
      if (sbIn->stmts[j]->tag == Ist_WrTmp) {
         /* findShadowTmpV checks its arg is an original tmp;
            no need to assert that here. */
         IRTemp tmp_o = sbIn->stmts[j]->Ist.WrTmp.tmp;
         IRTemp tmp_s = findShadowTmp(&ane, tmp_o);
         IRType ty_s  = typeOfIRTemp(sbIn->tyenv, tmp_s);
         assign( 'V', &ane, tmp_s, definedOfType( ty_s ) );
         if (0) {
            VG_(printf)("create shadow tmp for preamble tmp [%d] ty ", j);
            ppIRType( ty_s );
            VG_(printf)("\n");
         }
      }
   }
#endif

   /* Iterate over the remaining stmts to generate instrumentation. */

   tl_assert(sbIn->stmts_used > 0);
   tl_assert(i >= 0);
   tl_assert(i < sbIn->stmts_used);
   tl_assert(sbIn->stmts[i]->tag == Ist_IMark);

   for (/*use current i*/; i < sbIn->stmts_used; i++)
      schemeS( &ane, sbIn->stmts[i] );

   return ane.bb;
}


/*--------------------------------------------------------------------*/
/*--- Initialisation                                               ---*/
/*--------------------------------------------------------------------*/

static void an_post_clo_init ( void ); /* just below */
static void an_fini ( Int exitcode );  /* just below */

static void an_pre_clo_init ( void )
{
//zz    Int i;
//zz    // 0-terminated arrays
//zz    Addr compact_helpers[] = {
//zz       (Addr) do_lea1_unknown, (Addr) do_sub4,
//zz       (Addr) check_load4,     (Addr) check_store4,
//zz       (Addr) do_add4,         (Addr) do_and4,
//zz       (Addr) do_or4,          (Addr) nonptr_or_unknown,
//zz       0
//zz    };
//zz    Addr noncompact_helpers[] = {
//zz       (Addr) do_lea2_1,       (Addr) do_lea2_n,
//zz       (Addr) do_adc4,         (Addr) do_sbb4,
//zz       (Addr) do_neg4,         (Addr) do_not4,
//zz       (Addr) print_BB_entry,
//zz       (Addr) check_load1,     (Addr) check_store1,
//zz       (Addr) check_load2,     (Addr) check_store2,
//zz       (Addr) check_imul4,     (Addr) check_mul4,
//zz       (Addr) check_idiv4,     (Addr) check_div4,
//zz       (Addr) check_fpu_r2,    (Addr) check_fpu_w2,
//zz       (Addr) check_fpu_r4,    (Addr) check_fpu_w4,
//zz       (Addr) check_fpu_r8,    (Addr) check_fpu_w8,
//zz       (Addr) check_fpu_r10,   (Addr) check_fpu_w10,
//zz       (Addr) check_fpu_r16,   (Addr) check_fpu_w16,
//zz       (Addr) check_fpu_r28,   (Addr) check_fpu_w28,
//zz       (Addr) check_fpu_r108,  (Addr) check_fpu_w108,
//zz       0
//zz    };

   VG_(details_name)            ("Annelid");
   VG_(details_version)         ("0.0.2");
   VG_(details_description)     ("a pointer-use checker");
   VG_(details_copyright_author)(
      "Copyright (C) 2003, and GNU GPL'd, by Nicholas Nethercote.");
   VG_(details_bug_reports_to)  ("njn25@cam.ac.uk");

   VG_(basic_tool_funcs)( an_post_clo_init,
                          an_instrument,
                          an_fini );

   VG_(needs_malloc_replacement)( an_replace_malloc,
                                  an_replace___builtin_new,
                                  an_replace___builtin_vec_new,
                                  an_replace_memalign,
                                  an_replace_calloc,
                                  an_replace_free,
                                  an_replace___builtin_delete,
                                  an_replace___builtin_vec_delete,
                                  an_replace_realloc,
                                  AN_MALLOC_REDZONE_SZB );

   VG_(needs_core_errors)         ();
   VG_(needs_tool_errors)         (eq_Error,
                                   pp_Error,
                                   True,/*show TIDs for errors*/
                                   update_Error_extra,
                                   is_recognised_suppression,
                                   read_extra_suppression_info,
                                   error_matches_suppression,
                                   get_error_name,
                                   print_extra_suppression_info);

   VG_(needs_syscall_wrapper)( pre_syscall,
                               post_syscall );

//zz    // No needs
//zz    VG_(needs_core_errors)         ();
//zz    VG_(needs_skin_errors)         ();
//zz    VG_(needs_shadow_regs)         ();
//zz    VG_(needs_command_line_options)();
//zz    VG_(needs_syscall_wrapper)     ();
//zz    VG_(needs_sanity_checks)       ();
//zz 
//zz    // Memory events to track
   VG_(track_new_mem_startup)      ( new_mem_startup );
//zz    VG_(track_new_mem_stack_signal) ( NULL );
//zz    VG_(track_new_mem_brk)          ( new_mem_brk  );
   VG_(track_new_mem_mmap)         ( new_mem_mmap );
//zz 
//zz    VG_(track_copy_mem_remap)       ( copy_mem_remap );
//zz    VG_(track_change_mem_mprotect)  ( NULL );
//zz 
//zz    VG_(track_die_mem_stack_signal) ( NULL );
//zz    VG_(track_die_mem_brk)          ( die_mem_brk );
   VG_(track_die_mem_munmap)       ( die_mem_munmap );
//zz 
//zz    VG_(track_pre_mem_read)         ( pre_mem_access );
//zz    VG_(track_pre_mem_read_asciiz)  ( pre_mem_read_asciiz );
//zz    VG_(track_pre_mem_write)        ( pre_mem_access );
//zz    VG_(track_post_mem_write)       ( post_mem_write );

   // Register events to track
//zz    VG_(track_post_regs_write_init)             ( post_regs_write_init      );
//zz    VG_(track_post_reg_write_syscall_return)    ( post_reg_write_nonptr     );
//zz    VG_(track_post_reg_write_deliver_signal)    ( post_reg_write_nonptr_or_unknown );
//zz    VG_(track_post_reg_write_pthread_return)    ( post_reg_write_nonptr_or_unknown );
//zz    VG_(track_post_reg_write_clientreq_return)  ( post_reg_write_nonptr     );
   VG_(track_post_reg_write_clientcall_return) ( post_reg_write_clientcall );
   VG_(track_post_reg_write)( post_reg_write_demux );
//zz 
//zz    // Helpers
//zz    for (i = 0; compact_helpers[i] != 0; i++)
//zz       VG_(register_compact_helper)( compact_helpers[i] );
//zz 
//zz    for (i = 0; noncompact_helpers[i] != 0; i++)
//zz       VG_(register_noncompact_helper)( noncompact_helpers[i] );
//zz 
//zz    // Profiling events
//zz    #define P(a,b) VGP_(register_profile_event)(a,b);
//zz    P(VgpGetMemAseg,     "get-mem-aseg");
//zz    P(VgpCheckLoadStore, "check-load-store");
//zz    P(VgpCheckLoad4,     "check-load4");
//zz    P(VgpCheckLoad21,    "check-load21");
//zz    P(VgpCheckStore4,    "check-store4");
//zz    P(VgpCheckStore21,   "check-store21");
//zz    P(VgpCheckFpuR,      "check-fpu-r");
//zz    P(VgpCheckFpuW,      "check-fpu-w");
//zz    P(VgpDoAdd,          "do-add");
//zz    P(VgpDoSub,          "do-sub");
//zz    P(VgpDoAdcSbb,       "do-adc-sbb");
//zz    P(VgpDoXor,          "do-xor");
//zz    P(VgpDoAnd,          "do-and");
//zz    P(VgpDoOr,           "do-or");
//zz    P(VgpDoLea1,         "do-lea1");
//zz    P(VgpDoLea2,         "do-lea2");
//zz    #undef P

   // Other initialisation
   init_shadow_memory();
   seglist  = ISList__construct();

   // init_shadow_registers();
   // This is deferred until we are asked to instrument the
   // first SB.  Very ugly, but necessary since right now we can't
   // ask what the current ThreadId is.  See comment-ref KLUDGE01
   // above.
}

static void an_post_clo_init ( void )
{
}

/*--------------------------------------------------------------------*/
/*--- Finalisation                                                 ---*/
/*--------------------------------------------------------------------*/

static void an_fini ( Int exitcode )
{
//   if (0)
//      count_segs();
}

VG_DETERMINE_INTERFACE_VERSION(an_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                an_main.c ---*/
/*--------------------------------------------------------------------*/
