
/*--------------------------------------------------------------------*/
/*--- sgcheck: a stack/global array overrun checker.               ---*/
/*---                                                    sg_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of SGcheck, a Valgrind tool for checking 
   overruns in stack and global arrays in programs.

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

#include "pub_tool_basics.h"

#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"

#include "pub_tool_tooliface.h"

#include "pub_tool_wordfm.h"
#include "pub_tool_xarray.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_machine.h"
#include "pub_tool_options.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_replacemalloc.h"


static void preen_Invars ( Addr a, SizeT len, Bool isHeap ); /*fwds*/


//////////////////////////////////////////////////////////////
//                                                          //
// Basic Stuff                                              //
//                                                          //
//////////////////////////////////////////////////////////////

static inline Bool is_sane_TId ( ThreadId tid )
{
   return tid >= 0 && tid < VG_N_THREADS
          && tid != VG_INVALID_THREADID;
}

static void* pc_malloc ( SizeT n ) {
   void* p;
   tl_assert(n > 0);
   p = VG_(malloc)( n );
   tl_assert(p);
   return p;
}

static void pc_free ( void* p ) {
   tl_assert(p);
   VG_(free)(p);
}


#define MK_XAMAGIC(_c3,_c2,_c1,_c0) \
   ( ( ((UInt)(_c3)) << 24 ) | ( ((UInt)(_c2)) << 16 ) |  \
     ( ((UInt)(_c1)) << 8 )  | ( ((UInt)(_c0)) <<  0 ) )

#define StackBlock_XAMAGIC  MK_XAMAGIC('S','B','l','k')


/* Compare the intervals [a1,a1+n1) and [a2,a2+n2).  Return -1 if the
   first interval is lower, 1 if the first interval is higher, and 0
   if there is any overlap.  Redundant paranoia with casting is there
   following what looked distinctly like a bug in gcc-4.1.2, in which
   some of the comparisons were done signedly instead of
   unsignedly. */
inline
static Word cmp_nonempty_intervals ( Addr a1, SizeT n1, 
                                     Addr a2, SizeT n2 ) {
   UWord a1w = (UWord)a1;
   UWord n1w = (UWord)n1;
   UWord a2w = (UWord)a2;
   UWord n2w = (UWord)n2;
   tl_assert(n1w > 0 && n2w > 0);
   if (a1w + n1w <= a2w) return -1L;
   if (a2w + n2w <= a1w) return 1L;
   return 0;
}

/* Return true iff [aSmall,aSmall+nSmall) is entirely contained
   within [aBig,aBig+nBig). */
inline
static Bool is_subinterval_of ( Addr aBig, SizeT nBig,
                                Addr aSmall, SizeT nSmall ) {
   tl_assert(nBig > 0 && nSmall > 0);
   return aBig <= aSmall && aSmall + nSmall <= aBig + nBig;
}


//////////////////////////////////////////////////////////////
//                                                          //
// StackBlocks Persistent Cache                             //
//                                                          //
//////////////////////////////////////////////////////////////

/* We maintain a set of XArray* of StackBlocks.  These are never
   freed.  When a new StackBlock vector is acquired from
   VG_(di_get_local_blocks_at_ip), we compare it to the existing set.
   If not present, it is added.  If present, the just-acquired one is
   freed and the copy used.

   This simplifies storage management elsewhere.  It allows us to
   assume that a pointer to an XArray* of StackBlock is valid forever.
   It also means there are no duplicates anywhere, which could be
   important from a space point of view for programs that generate a
   lot of translations, or where translations are frequently discarded
   and re-made.

   Note that we normalise the arrays by sorting the elements according
   to an arbitrary total order, so as to avoid the situation that two
   vectors describe the same set of variables but are not structurally
   identical. */

static inline Bool StackBlock__sane ( StackBlock* fb )
{
   if (fb->name[ sizeof(fb->name)-1 ] != 0)
      return False;
   if (fb->spRel != False && fb->spRel != True)
      return False;
   if (fb->isVec != False && fb->isVec != True)
      return False;
   return True;
}

/* Generate an arbitrary total ordering on StackBlocks. */
static Word StackBlock__cmp ( StackBlock* fb1, StackBlock* fb2 )
{
   Word r;
   tl_assert(StackBlock__sane(fb1));
   tl_assert(StackBlock__sane(fb2));
   /* Hopefully the .base test hits most of the time.  For the blocks
      associated with any particular instruction, if the .base values
      are the same then probably it doesn't make sense for the other
      fields to be different.  But this is supposed to be a completely
      general structural total order, so we have to compare everything
      anyway. */
   if (fb1->base < fb2->base) return -1;
   if (fb1->base > fb2->base) return 1;
   /* compare sizes */
   if (fb1->szB < fb2->szB) return -1;
   if (fb1->szB > fb2->szB) return 1;
   /* compare sp/fp flag */
   if (fb1->spRel < fb2->spRel) return -1;
   if (fb1->spRel > fb2->spRel) return 1;
   /* compare is/is-not array-typed flag */
   if (fb1->isVec < fb2->isVec) return -1;
   if (fb1->isVec > fb2->isVec) return 1;
   /* compare the name */
   r = (Word)VG_(strcmp)(fb1->name, fb2->name);
   return r;
}

/* Generate an arbitrary total ordering on vectors of StackBlocks. */
static Word StackBlocks__cmp ( XArray* fb1s, XArray* fb2s )
{
   Word i, r, n1, n2;
   n1 = VG_(sizeXA)( fb1s );
   n2 = VG_(sizeXA)( fb2s );
   if (n1 < n2) return -1;
   if (n1 > n2) return 1;
   for (i = 0; i < n1; i++) {
      StackBlock *fb1, *fb2;
      fb1 = VG_(indexXA)( fb1s, i );
      fb2 = VG_(indexXA)( fb2s, i );
      r = StackBlock__cmp( fb1, fb2 );
      if (r != 0) return r;
   }
   tl_assert(i == n1 && i == n2);
   return 0;
}

static void pp_StackBlock ( StackBlock* sb )
{
   VG_(printf)("StackBlock{ off %ld szB %lu spRel:%c isVec:%c \"%s\" }",
               sb->base, sb->szB, sb->spRel ? 'Y' : 'N',
               sb->isVec ? 'Y' : 'N', &sb->name[0] );
}

static void pp_StackBlocks ( XArray* sbs )
{
   Word i, n = VG_(sizeXA)( sbs );
   VG_(printf)("<<< STACKBLOCKS\n" );
   for (i = 0; i < n; i++) {
      VG_(printf)("   ");
      pp_StackBlock( (StackBlock*)VG_(indexXA)( sbs, i ) );
      VG_(printf)("\n");
   }
   VG_(printf)(">>> STACKBLOCKS\n" );
}


/* ---------- The StackBlock vector cache ---------- */

static WordFM* /* XArray* of StackBlock -> nothing */
       frameBlocks_set = NULL;

static void init_StackBlocks_set ( void )
{
   tl_assert(!frameBlocks_set);
   frameBlocks_set = VG_(newFM)( pc_malloc, pc_free, 
                                 (Word(*)(UWord,UWord))StackBlocks__cmp );
   tl_assert(frameBlocks_set);
}

/* Find the given StackBlock-vector in our collection thereof.  If
   found, deallocate the supplied one, and return the address of the
   copy.  If not found, add the supplied one to our collection and
   return its address. */
static XArray* /* of StackBlock */
       StackBlocks__find_and_dealloc__or_add
          ( XArray* /* of StackBlock */ orig )
{
   UWord key, val;

   tl_assert( VG_(getMagicXA)(orig) == 0 ); /* as yet unset */

   /* First, normalise, as per comments above. */
   VG_(setCmpFnXA)( orig, (Int(*)(void*,void*))StackBlock__cmp );
   VG_(sortXA)( orig );

   /* Now get rid of any duplicates. */
   { Word r, w, n = VG_(sizeXA)( orig );
     if (n >= 2) {
        w = 0;
        for (r = 0; r < n; r++) {
           if (r+1 < n) {
              StackBlock* pR0 = VG_(indexXA)( orig, r+0 );
              StackBlock* pR1 = VG_(indexXA)( orig, r+1 );
              Word c = StackBlock__cmp(pR0,pR1);
              tl_assert(c == -1 || c == 0);
              if (c == 0) continue;
           }
           if (w != r) {
              StackBlock* pW = VG_(indexXA)( orig, w );
              StackBlock* pR = VG_(indexXA)( orig, r );
              *pW = *pR;
           }
           w++;
        }
        tl_assert(r == n);
        tl_assert(w <= n);
        if (w < n) {
           VG_(dropTailXA)( orig, n-w );
        }
        if (0) VG_(printf)("delta %ld\n", n-w);
     }
   }

   { Word i, n = VG_(sizeXA)( orig );
   for (i = 0; i < n-1; i++) {
     StackBlock* sb1 = (StackBlock*)VG_(indexXA)( orig, i );
     StackBlock* sb2 = (StackBlock*)VG_(indexXA)( orig, i+1 );
     if (sb1->base == sb2->base)
       pp_StackBlocks(orig);
     tl_assert(sb1->base != sb2->base);
   }
   }

   /* Now, do we have it already? */
   if (VG_(lookupFM)( frameBlocks_set, &key, &val, (UWord)orig )) {
      /* yes */
      XArray* res;
      tl_assert(val == 0);
      tl_assert(key != (UWord)orig);
      VG_(deleteXA)(orig);
      res = (XArray*)key;
      tl_assert( VG_(getMagicXA)(res) == StackBlock_XAMAGIC );
      return res;
   } else {
      /* no */
      VG_(setMagicXA)( orig, StackBlock_XAMAGIC );
      VG_(addToFM)( frameBlocks_set, (UWord)orig, 0 );
      return orig;
   }
}

/* Top level function for getting the StackBlock vector for a given
   instruction.  It is guaranteed that the returned pointer will be
   valid for the entire rest of the run, and also that the addresses
   of the individual elements of the array will not change. */

static XArray* /* of StackBlock */ get_StackBlocks_for_IP ( Addr ip )
{
   XArray* blocks = VG_(di_get_stack_blocks_at_ip)( ip, True/*arrays only*/ );
   tl_assert(blocks);
   return StackBlocks__find_and_dealloc__or_add( blocks );
}


//////////////////////////////////////////////////////////////
//                                                          //
// GlobalBlocks Persistent Cache                            //
//                                                          //
//////////////////////////////////////////////////////////////

/* Generate an arbitrary total ordering on GlobalBlocks. */
static Word GlobalBlock__cmp ( GlobalBlock* gb1, GlobalBlock* gb2 )
{
   Word r;
   /* compare addrs */
   if (gb1->addr < gb2->addr) return -1;
   if (gb1->addr > gb2->addr) return 1;
   /* compare sizes */
   if (gb1->szB < gb2->szB) return -1;
   if (gb1->szB > gb2->szB) return 1;
   /* compare is/is-not array-typed flag */
   if (gb1->isVec < gb2->isVec) return -1;
   if (gb1->isVec > gb2->isVec) return 1;
   /* compare the name */
   r = (Word)VG_(strcmp)(gb1->name, gb2->name);
   if (r != 0) return r;
   /* compare the soname */
   r = (Word)VG_(strcmp)(gb1->soname, gb2->soname);
   return r;
}

static WordFM* /* GlobalBlock* -> nothing */
       globalBlock_set = NULL;

static void init_GlobalBlock_set ( void )
{
   tl_assert(!globalBlock_set);
   globalBlock_set = VG_(newFM)( pc_malloc, pc_free, 
                                 (Word(*)(UWord,UWord))GlobalBlock__cmp );
   tl_assert(globalBlock_set);
}


/* Top level function for making GlobalBlocks persistent.  Call here
   with a non-persistent version, and the returned one is guaranteed
   to be valid for the entire rest of the run.  The supplied one is
   copied, not stored, so can be freed after the call. */

static GlobalBlock* get_persistent_GlobalBlock ( GlobalBlock* orig )
{
   UWord key, val;
   /* Now, do we have it already? */
   if (VG_(lookupFM)( globalBlock_set, &key, &val, (UWord)orig )) {
      /* yes, return the copy */
      GlobalBlock* res;
      tl_assert(val == 0);
      res = (GlobalBlock*)key;
      tl_assert(res != orig);
      return res;
   } else {
      /* no.  clone it, store the clone and return the clone's
         address. */
      GlobalBlock* clone = pc_malloc( sizeof(GlobalBlock) );
      tl_assert(clone);
      *clone = *orig;
      VG_(addToFM)( globalBlock_set, (UWord)clone, 0 );
      return clone;
   }
}


//////////////////////////////////////////////////////////////
//                                                          //
// Interval tree of StackTreeBlock                          //
//                                                          //
//////////////////////////////////////////////////////////////

/* A node in a stack interval tree.  Zero length intervals (.szB == 0)
   are not allowed.

   A stack interval tree is a (WordFM StackTreeNode* void).  There is
   one stack interval tree for each thread.
*/
typedef
   struct {
      Addr        addr;
      SizeT       szB;   /* copied from .descr->szB */
      StackBlock* descr; /* it's an instance of this block */
      UWord       depth; /* depth of stack at time block was pushed */
   }
   StackTreeNode;

static void pp_StackTree ( WordFM* sitree, HChar* who )
{
   UWord keyW, valW;
   VG_(printf)("<<< BEGIN pp_StackTree %s\n", who );
   VG_(initIterFM)( sitree );
   while (VG_(nextIterFM)( sitree, &keyW, &valW )) {
      StackTreeNode* nd = (StackTreeNode*)keyW;
      VG_(printf)("  [%#lx,+%lu) descr=%p %s %lu\n", nd->addr, nd->szB,
                  nd->descr, nd->descr->name, nd->descr->szB);
   }
   VG_(printf)(">>> END   pp_StackTree %s\n", who );
}

/* Interval comparison function for StackTreeNode */
static Word cmp_intervals_StackTreeNode ( StackTreeNode* sn1,
                                          StackTreeNode* sn2 )
{
   return cmp_nonempty_intervals(sn1->addr, sn1->szB,
                                 sn2->addr, sn2->szB);
}

/* Find the node holding 'a', if any. */
static StackTreeNode* find_StackTreeNode ( WordFM* sitree, Addr a )
{
   UWord keyW, valW;
   StackTreeNode key;
   tl_assert(sitree);
   key.addr = a;
   key.szB  = 1;
   if (VG_(lookupFM)( sitree, &keyW, &valW, (UWord)&key )) {
      StackTreeNode* res = (StackTreeNode*)keyW;
      tl_assert(valW == 0);
      tl_assert(res != &key);
      return res;
   } else {
      return NULL;
   }
}

/* Note that the supplied XArray of FrameBlock must have been
   made persistent already. */
__attribute__((noinline))
static void add_blocks_to_StackTree (
               /*MOD*/WordFM* sitree,
               XArray* /* FrameBlock */ descrs,
               XArray* /* Addr */ bases,
               UWord depth
            )
{
   Bool debug = (Bool)0;
   Word i, nDescrs, nBases;

   nDescrs = VG_(sizeXA)( descrs ),
   nBases = VG_(sizeXA)( bases );
   tl_assert(nDescrs == nBases);

   if (nDescrs == 0) return;

   tl_assert(sitree);
   if (debug) {
      VG_(printf)("\n");
      pp_StackTree( sitree, "add_blocks_to_StackTree-pre" );
   }

   for (i = 0; i < nDescrs; i++) {
      Bool already_present;
      StackTreeNode* nyu;
      Addr        addr  = *(Addr*)VG_(indexXA)( bases, i );
      StackBlock* descr = (StackBlock*)VG_(indexXA)( descrs, i );
      tl_assert(descr->szB > 0);
      nyu = pc_malloc( sizeof(StackTreeNode) );
      nyu->addr  = addr;
      nyu->szB   = descr->szB;
      nyu->descr = descr;
      nyu->depth = depth;
      if (debug) VG_(printf)("ADD %#lx %lu\n", addr, descr->szB);
      already_present = VG_(addToFM)( sitree, (UWord)nyu, 0 );
      /* The interval can't already be there; else we have
         overlapping stack blocks. */
      tl_assert(!already_present);
      if (debug) {
         pp_StackTree( sitree, "add_blocks_to_StackTree-step" );
      }
   }
   if (debug) {
      pp_StackTree( sitree, "add_blocks_to_StackTree-post" );
      VG_(printf)("\n");
   }
}

static void del_blocks_from_StackTree ( /*MOD*/WordFM* sitree,
                                        XArray* /* Addr */ bases ) 
{
   UWord oldK, oldV;
   Word i, nBases = VG_(sizeXA)( bases );
   for (i = 0; i < nBases; i++) {
      Bool b;
      Addr addr = *(Addr*)VG_(indexXA)( bases, i );
      StackTreeNode* nd = find_StackTreeNode(sitree, addr);
      /* The interval must be there; we added it earlier when
         the associated frame was created. */
      tl_assert(nd);
      b = VG_(delFromFM)( sitree, &oldK, &oldV, (UWord)nd );
      /* we just found the block! */
      tl_assert(b);
      tl_assert(oldV == 0);
      tl_assert(nd == (StackTreeNode*)oldK);
      pc_free(nd);
   }
}


static void delete_StackTree__kFin ( UWord keyW ) {
   StackTreeNode* nd = (StackTreeNode*)keyW;
   tl_assert(nd);
   pc_free(nd);
}
static void delete_StackTree__vFin ( UWord valW ) {
   tl_assert(valW == 0);
}
static void delete_StackTree ( WordFM* sitree )
{
   VG_(deleteFM)( sitree,
                 delete_StackTree__kFin, delete_StackTree__vFin );
}

static WordFM* new_StackTree ( void ) {
   return VG_(newFM)( pc_malloc, pc_free,
                      (Word(*)(UWord,UWord))cmp_intervals_StackTreeNode );
}


//////////////////////////////////////////////////////////////
//                                                          //
// Interval tree of GlobalTreeBlock                         //
//                                                          //
//////////////////////////////////////////////////////////////

/* A node in a global interval tree.  Zero length intervals 
   (.szB == 0) are not allowed.

   A global interval tree is a (WordFM GlobalTreeNode* void).  There
   is one global interval tree for the entire process.
*/
typedef
   struct {
      Addr         addr; /* copied from .descr->addr */
      SizeT        szB; /* copied from .descr->szB */
      GlobalBlock* descr; /* it's this block */
   }
   GlobalTreeNode;

/* Interval comparison function for GlobalTreeNode */
static Word cmp_intervals_GlobalTreeNode ( GlobalTreeNode* gn1,
                                           GlobalTreeNode* gn2 )
{
   return cmp_nonempty_intervals( gn1->addr, gn1->szB,
                                  gn2->addr, gn2->szB );
}

/* Find the node holding 'a', if any. */
static GlobalTreeNode* find_GlobalTreeNode ( WordFM* gitree, Addr a )
{
   UWord keyW, valW;
   GlobalTreeNode key;
   key.addr = a;
   key.szB  = 1;
   if (VG_(lookupFM)( gitree, &keyW, &valW, (UWord)&key )) {
      GlobalTreeNode* res = (GlobalTreeNode*)keyW;
      tl_assert(valW == 0);
      tl_assert(res != &key);
      return res;
   } else {
      return NULL;
   }
}

/* Note that the supplied GlobalBlock must have been made persistent
   already. */
static void add_block_to_GlobalTree (
               /*MOD*/WordFM* gitree,
               GlobalBlock* descr
            )
{
   Bool already_present;
   GlobalTreeNode* nyu;
   tl_assert(descr->szB > 0);
   nyu = pc_malloc( sizeof(GlobalTreeNode) );
   nyu->addr  = descr->addr;
   nyu->szB   = descr->szB;
   nyu->descr = descr;
   already_present = VG_(addToFM)( gitree, (UWord)nyu, 0 );
   /* The interval can't already be there; else we have
      overlapping global blocks. */
   tl_assert(!already_present);
}

static Bool del_GlobalTree_range ( /*MOD*/WordFM* gitree,
                                   Addr a, SizeT szB )
{
   /* One easy way to do this: look up [a,a+szB) in the tree.  That
      will either succeed, producing a block which intersects that
      range, in which case we delete it and repeat; or it will fail,
      in which case there are no blocks intersecting the range, and we
      can bring the process to a halt. */
   UWord keyW, valW, oldK, oldV;
   GlobalTreeNode key, *nd;
   Bool b, anyFound;

   tl_assert(szB > 0);

   anyFound = False;

   key.addr = a;
   key.szB  = szB;

   while (VG_(lookupFM)( gitree, &keyW, &valW, (UWord)&key )) {
      anyFound = True;
      nd = (GlobalTreeNode*)keyW;
      tl_assert(valW == 0);
      tl_assert(nd != &key);
      tl_assert(cmp_nonempty_intervals(a, szB, nd->addr, nd->szB) == 0);

      b = VG_(delFromFM)( gitree, &oldK, &oldV, (UWord)&key );
      tl_assert(b);
      tl_assert(oldV == 0);
      tl_assert(oldK == keyW); /* check we deleted the node we just found */
   }

   return anyFound;
}


//////////////////////////////////////////////////////////////
//                                                          //
// our globals                                              //
//                                                          //
//////////////////////////////////////////////////////////////

/* Each thread has:
   * a shadow stack of StackFrames
   * an stack block interval tree
*/
static XArray* /* StackFrame */    shadowStacks[VG_N_THREADS];

static WordFM* /* StackTreeNode */ siTrees[VG_N_THREADS];

/* Additionally, there is one global variable interval tree
   for the entire process.
*/
static WordFM* /* GlobalTreeNode */ giTree;


static void ourGlobals_init ( void )
{
   Word i;
   for (i = 0; i < VG_N_THREADS; i++) {
      shadowStacks[i] = NULL;
      siTrees[i] = NULL;
   }
   giTree = VG_(newFM)( pc_malloc, pc_free, 
                        (Word(*)(UWord,UWord))cmp_intervals_GlobalTreeNode );
}


//////////////////////////////////////////////////////////////
//                                                          //
// Handle global variable load/unload events                //
//                                                          //
//////////////////////////////////////////////////////////////

static void acquire_globals ( ULong di_handle )
{
   Word n, i;
   XArray* /* of GlobalBlock */ gbs;
   if (0) VG_(printf)("ACQUIRE GLOBALS %llu\n", di_handle );
   gbs = VG_(di_get_global_blocks_from_dihandle)
            (di_handle, True/*arrays only*/);
   if (0) VG_(printf)("   GOT %ld globals\n", VG_(sizeXA)( gbs ));

   n = VG_(sizeXA)( gbs );
   for (i = 0; i < n; i++) {
      GlobalBlock* gbp;
      GlobalBlock* gb = VG_(indexXA)( gbs, i );
      VG_(printf)("   new Global size %2lu at %#lx:  %s %s\n", 
                  gb->szB, gb->addr, gb->soname, gb->name );
      tl_assert(gb->szB > 0);
      /* Make a persistent copy of each GlobalBlock, and add it
         to the tree. */
      gbp = get_persistent_GlobalBlock( gb );
      add_block_to_GlobalTree( giTree, gbp );
   }

   VG_(deleteXA)( gbs );
}


/* We only intercept these two because we need to see any di_handles
   that might arise from the mappings/allocations. */
static void sg_new_mem_mmap( Addr a, SizeT len,
                             Bool rr, Bool ww, Bool xx, ULong di_handle )
{
   if (di_handle > 0)
      acquire_globals(di_handle);
}
static void sg_new_mem_startup( Addr a, SizeT len,
                             Bool rr, Bool ww, Bool xx, ULong di_handle )
{
   if (di_handle > 0)
      acquire_globals(di_handle);
}
static void sg_die_mem_munmap ( Addr a, SizeT len )
{
   Bool debug = (Bool)0;
   Bool overlap = False;

   if (debug) VG_(printf)("MUNMAP %#lx %lu\n", a, len );

   if (len == 0)
      return;

   overlap = del_GlobalTree_range(giTree, a, len);

   { /* redundant sanity check */
     UWord keyW, valW;
     VG_(initIterFM)( giTree );
     while (VG_(nextIterFM)( giTree, &keyW, &valW )) {
       GlobalTreeNode* nd = (GlobalTreeNode*)keyW;
        tl_assert(valW == 0);
        tl_assert(nd->szB > 0);
        tl_assert(nd->addr + nd->szB <= a
                  || a + len <= nd->addr);
     }
     VG_(doneIterFM)( giTree );
   }

   if (!overlap)
      return;

   /* Ok, the range contained some blocks.  Therefore we'll need to
      visit all the Invars in all the thread shadow stacks, and
      convert all Inv_Global{S,V} entries that intersect [a,a+len) to
      Inv_Unknown. */
   tl_assert(len > 0);
   preen_Invars( a, len, False/*!isHeap*/ );
}


//////////////////////////////////////////////////////////////
//                                                          //
// Invar                                                    //
//                                                          //
//////////////////////////////////////////////////////////////

/* An invariant, as resulting from watching the destination of a
   memory referencing instruction.  Initially is Inv_Unset until the
   instruction makes a first access. */

typedef
   enum {
      Inv_Unset=1,  /* not established yet */
      Inv_Unknown,  /* unknown location */
      Inv_Stack0,   /* array-typed stack block in innermost frame */
      Inv_StackN,   /* array-typed stack block in non-innermost frame */
      Inv_Global,   /* array-typed global block */
   }
   InvarTag;

typedef
   struct {
      InvarTag tag;
      union {
         struct {
         } Unset;
         struct {
         } Unknown;
         struct {
            Addr  addr;
            SizeT szB;
            StackBlock* descr;
         } Stack0; /* innermost stack frame */
         struct {
            /* Pointer to a node in the interval tree for
              this thread. */
            StackTreeNode* nd;
         } StackN; /* non-innermost stack frame */
         struct {
           /* Pointer to a GlobalBlock in the interval tree of
              global blocks. */
           GlobalTreeNode* nd;
         } Global;
      }
      Inv;
   }
   Invar;

/* Compare two Invars for equality. */
static Bool eq_Invar ( Invar* i1, Invar* i2 )
{
   tl_assert(i1->tag != Inv_Unset);
   tl_assert(i2->tag != Inv_Unset);
   if (i1->tag != i2->tag)
      return False;
   switch (i1->tag) {
      case Inv_Unknown:
         return True;
      case Inv_Stack0:
         return i1->Inv.Stack0.addr == i2->Inv.Stack0.addr
                && i1->Inv.Stack0.szB == i2->Inv.Stack0.szB;
      case Inv_StackN:
         return i1->Inv.StackN.nd == i2->Inv.StackN.nd;
      case Inv_Global:
         return i1->Inv.Global.nd == i2->Inv.Global.nd;
      default:
         tl_assert(0);
   }
   /*NOTREACHED*/
   tl_assert(0);
}

/* Print selected parts of an Invar, suitable for use in error
   messages. */
static void show_Invar( HChar* buf, Word nBuf, Invar* inv, Word depth )
{
   HChar* str;
   tl_assert(nBuf >= 128);
   buf[0] = 0;
   switch (inv->tag) {
      case Inv_Unknown:
         VG_(sprintf)(buf, "%s", "unknown");
         break;
      case Inv_Stack0:
         str = "array";
         VG_(sprintf)(buf, "stack %s \"%s\" in this frame",
                      str, inv->Inv.Stack0.descr->name );
         break;
      case Inv_StackN:
         str = "array";
         VG_(sprintf)(buf, "stack %s \"%s\" in frame %lu back from here",
                      str, inv->Inv.StackN.nd->descr->name,
                           depth - inv->Inv.StackN.nd->depth );
         break;
      case Inv_Global:
         str = "array";
         VG_(sprintf)(buf, "global %s \"%s\" in object with soname \"%s\"",
                      str, inv->Inv.Global.nd->descr->name,
                           inv->Inv.Global.nd->descr->soname );
         break;
      case Inv_Unset:
         VG_(sprintf)(buf, "%s", "Unset!");
         break;
      default:
         tl_assert(0);
   }
}


//////////////////////////////////////////////////////////////
//                                                          //
// StackFrame                                               //
//                                                          //
//////////////////////////////////////////////////////////////

static ULong stats__total_accesses   = 0;
static ULong stats__classify_Stack0  = 0;
static ULong stats__classify_StackN  = 0;
static ULong stats__classify_Global  = 0;
static ULong stats__classify_Unknown = 0;
static ULong stats__Invars_preened   = 0;
static ULong stats__Invars_changed   = 0;

/* A dynamic instance of an instruction */
typedef
   struct {
      /* IMMUTABLE */
      Addr    insn_addr; /* NB! zero means 'not in use' */
      XArray* blocks; /* XArray* of StackBlock */
      /* MUTABLE */
      Invar invar;
   }
   IInstance;


typedef
   struct {
      /* The sp when the frame was created, so we know when to get rid
         of it. */
      Addr creation_sp;
      /* Information for each memory referencing instruction, for this
         instantiation of the function.  The iinstances array is
         operated as a simple linear-probe hash table, which is
         dynamically expanded as necessary.  Once critical thing is
         that an IInstance with a .insn_addr of zero is interpreted to
         mean that hash table slot is unused.  This means we can't
         store an IInstance for address zero. */
      IInstance* htab;
      UWord      htab_size; /* size of hash table, MAY ONLY BE A POWER OF 2 */
      UWord      htab_used; /* number of hash table slots currently in use */
      /* If this frame is currently making a call, then the following
         are relevant. */
      Addr sp_at_call;
      Addr fp_at_call;
      XArray* /* of Addr */ blocks_added_by_call;
   }
   StackFrame;





/* Move this somewhere else? */
/* Visit all Invars in the entire system.  If 'isHeap' is True, change
   all Inv_Heap Invars that intersect [a,a+len) to Inv_Unknown.  If
   'isHeap' is False, do the same but to the Inv_Global{S,V} Invars
   instead. */

__attribute__((noinline))
static void preen_Invar ( Invar* inv, Addr a, SizeT len, Bool isHeap )
{
   stats__Invars_preened++;
   tl_assert(len > 0);
   tl_assert(inv);
   switch (inv->tag) {
#if 0
      case Inv_Heap:
         tl_assert(inv->Inv.Heap.len > 0);
         if (isHeap && rangesOverlap(a, len, inv->Inv.Heap.start,
                                             inv->Inv.Heap.len)) {
            inv->tag = Inv_Unknown;
            stats__Invars_changed++;
         }
         break;
      case Inv_GlobalS:
      case Inv_GlobalV:
         tl_assert(inv->Inv.Global.len > 0);
         if ((!isHeap)
             && rangesOverlap(a, len, inv->Inv.Global.start,
                                      inv->Inv.Global.len)) {
            inv->tag = Inv_Unknown;
            stats__Invars_changed++;
         }
         break;
      case Inv_StackS:
      case Inv_StackV:
      case Inv_Unknown:
         break;
#endif
      default: tl_assert(0);
   }
}

__attribute__((noinline))
static void preen_Invars ( Addr a, SizeT len, Bool isHeap )
{
   Int         i;
   Word        ixFrames, nFrames;
   UWord       u;
   XArray*     stack; /* XArray* of StackFrame */
   StackFrame* frame;
   tl_assert(len > 0);
   for (i = 0; i < VG_N_THREADS; i++) {
      stack = shadowStacks[i];
      if (!stack)
         continue;
      nFrames = VG_(sizeXA)( stack );
      for (ixFrames = 0; ixFrames < nFrames; ixFrames++) {
         UWord xx = 0; /* sanity check only; count of used htab entries */
         frame = VG_(indexXA)( stack, ixFrames );
         tl_assert(frame->htab);
         for (u = 0; u < frame->htab_size; u++) {
            IInstance* ii = &frame->htab[u];
            if (ii->insn_addr == 0)
               continue; /* not in use */
            preen_Invar( &ii->invar, a, len, isHeap );
            xx++;           
         }
         tl_assert(xx == frame->htab_used);
      }
   }
}


__attribute__((noinline))
static void initialise_hash_table ( StackFrame* sf )
{
   UWord i;
   sf->htab_size = 4; /* initial hash table size */
   sf->htab = pc_malloc(sf->htab_size * sizeof(IInstance));
   tl_assert(sf->htab);
   sf->htab_used = 0;
   for (i = 0; i < sf->htab_size; i++)
      sf->htab[i].insn_addr = 0; /* NOT IN USE */
}


__attribute__((noinline))
static void resize_hash_table ( StackFrame* sf )
{
   UWord     i, j, ix, old_size, new_size;
   IInstance *old_htab, *new_htab, *old;

   tl_assert(sf && sf->htab);
   old_size = sf->htab_size;
   new_size = 2 * old_size;
   old_htab = sf->htab;
   new_htab = pc_malloc( new_size * sizeof(IInstance) );
   for (i = 0; i < new_size; i++) {
      new_htab[i].insn_addr = 0; /* NOT IN USE */
   }
   for (i = 0; i < old_size; i++) {
      old = &old_htab[i];
      if (old->insn_addr == 0 /* NOT IN USE */)
         continue;
      ix = (old->insn_addr >> 0) & (new_size - 1);
      /* find out where to put this, in the new table */
      j = new_size;
      while (1) {
         if (new_htab[ix].insn_addr == 0)
            break;
         /* This can't ever happen, because it would mean the new
            table is full; that isn't allowed -- even the old table is
            only allowed to become half full. */
         tl_assert(j > 0);
         j--;
         ix++; if (ix == new_size) ix = 0;
      }
      /* copy the old entry to this location */
      tl_assert(ix < new_size);
      tl_assert(new_htab[ix].insn_addr == 0);
      new_htab[ix] = *old;
      tl_assert(new_htab[ix].insn_addr != 0);
   }
   /* all entries copied; free old table. */
   pc_free(old_htab);
   sf->htab = new_htab;
   sf->htab_size = new_size;
   /* check sf->htab_used is correct.  Optional and a bit expensive
      but anyway: */
   j = 0;
   for (i = 0; i < new_size; i++) {
     if (new_htab[i].insn_addr != 0) {
       j++;
     }
   }
   tl_assert(j == sf->htab_used);
   if (0) VG_(printf)("resized tab for SF %p to %lu\n", sf, new_size);
}


__attribute__((noinline))
static IInstance* find_or_create_IInstance (
                     StackFrame* sf, 
                     Addr ip,
                     XArray* /* StackBlock */ ip_frameblocks
                  )
{
   UWord i, ix;
  start_over:
   tl_assert(sf);
   tl_assert(sf->htab);

   if (0) VG_(printf)("XXX ip %#lx size %lu used %lu\n",
                      ip, sf->htab_size, sf->htab_used);
   tl_assert(2 * sf->htab_used <= sf->htab_size);
  
   ix = (ip >> 0) & (sf->htab_size - 1);
   i = sf->htab_size;
   while (1) {
      if (sf->htab[ix].insn_addr == ip)
         return &sf->htab[ix];
      if (sf->htab[ix].insn_addr == 0)
         break;
      /* If i ever gets to zero and we have found neither what we're
         looking for nor an empty slot, the table must be full.  Which
         isn't possible -- we monitor the load factor to ensure it
         doesn't get above say 50%; if that ever does happen the table
         is resized. */
      tl_assert(i > 0);
      i--;
      ix++;
      if (ix == sf->htab_size) ix = 0;
   }

   /* So now we've found a free slot at ix, and we can use that.
      Except, first check if we need to resize the table.  If so,
      resize it, and start all over again. */
   tl_assert(sf->htab[ix].insn_addr == 0);
   if (2 * sf->htab_used >= 1 * sf->htab_size) {
      resize_hash_table(sf);
      goto start_over;
   }

   /* Add a new record in this slot. */
   tl_assert(ip != 0); /* CAN'T REPRESENT THIS */
   sf->htab[ix].insn_addr = ip;
   sf->htab[ix].blocks    = ip_frameblocks;
   sf->htab[ix].invar.tag = Inv_Unset;
   sf->htab_used++;
   return &sf->htab[ix];
}


__attribute__((noinline))
static Addr calculate_StackBlock_EA ( StackBlock* descr,
                                      Addr sp, Addr fp ) {
   UWord w1 = (UWord)descr->base;
   UWord w2 = (UWord)(descr->spRel ? sp : fp);
   UWord ea = w1 + w2;
   return ea;
}

/* Given an array of StackBlocks, return an array of Addrs, holding
   their effective addresses.  Caller deallocates result array. */
__attribute__((noinline))
static XArray* /* Addr */ calculate_StackBlock_EAs (
                             XArray* /* StackBlock */ blocks,
                             Addr sp, Addr fp
                          )
{
   XArray* res = VG_(newXA)( pc_malloc, pc_free, sizeof(Addr) );
   Word i, n = VG_(sizeXA)( blocks );
   for (i = 0; i < n; i++) {
      StackBlock* blk = VG_(indexXA)( blocks, i );
      Addr ea = calculate_StackBlock_EA( blk, sp, fp );
      VG_(addToXA)( res, &ea );
   }
   return res;
}


/* Try to classify the block into which a memory access falls, and
   write the result in 'inv'.  This writes all fields of 'inv',
   including, importantly the ReVal (revalidation) fields. */
__attribute__((noinline)) 
static void classify_address ( /*OUT*/Invar* inv,
                               ThreadId tid,
                               Addr ea, Addr sp, Addr fp,
                               UWord szB,
                               XArray* /* of StackBlock */ thisInstrBlocks,
                               XArray* /* of StackFrame */ thisThreadFrames )
{
   tl_assert(szB > 0);
   /* First, look in the stack blocks accessible in this instruction's
      frame. */
   { 
     Word i, nBlocks = VG_(sizeXA)( thisInstrBlocks );
     for (i = 0; i < nBlocks; i++) {
        StackBlock* descr = VG_(indexXA)( thisInstrBlocks, i );
        Addr bea = calculate_StackBlock_EA( descr, sp, fp );
        if (bea <= ea && ea + szB <= bea + descr->szB) {
           /* found it */
           inv->tag = Inv_Stack0;
           inv->Inv.Stack0.addr  = bea;
           inv->Inv.Stack0.szB   = descr->szB;
           inv->Inv.Stack0.descr = descr;
           stats__classify_Stack0++;
           return;
        }
     }
   }
   /* Ok, so it's not a block in the top frame.  Perhaps it's a block
      in some calling frame?  Consult this thread's stack-block
      interval tree to find out. */
   { StackTreeNode* nd = find_StackTreeNode( siTrees[tid], ea );
     /* We know that [ea,ea+1) is in the block, but we need to
        restrict to the case where the whole access falls within
        it. */
     if (nd && !is_subinterval_of(nd->addr, nd->szB, ea, szB)) {
        nd = NULL;
     }
     if (nd) {
        /* found it */
        inv->tag = Inv_StackN;
        inv->Inv.StackN.nd = nd;
           stats__classify_StackN++;
        return;
     }
   }
   /* Not in a stack block.  Try the global pool. */
   { GlobalTreeNode* nd = find_GlobalTreeNode(giTree, ea);
     /* We know that [ea,ea+1) is in the block, but we need to
        restrict to the case where the whole access falls within
        it. */
     if (nd && !is_subinterval_of(nd->addr, nd->szB, ea, szB)) {
        nd = NULL;
     }
     if (nd) {
       /* found it */
        inv->tag = Inv_Global;
        inv->Inv.Global.nd = nd;
        stats__classify_Global++;
        return;
     }
   }
   /* No idea - give up. */
   inv->tag = Inv_Unknown;
   stats__classify_Unknown++;
}


/* CALLED FROM GENERATED CODE */
static 
VG_REGPARM(3)
void helperc__mem_access ( /* Known only at run time: */
                           Addr ea, Addr sp, Addr fp,
                           /* Known at translation time: */
                           Word sszB, Addr ip, XArray* ip_frameBlocks )
{
   Word nFrames;
   UWord szB;
   XArray* /* of StackFrame */ frames;
   IInstance* iinstance;
   Invar* inv;
   Invar new_inv;
   ThreadId tid = VG_(get_running_tid)();
   StackFrame* frame;
   HChar buf[160];

   stats__total_accesses++;

   tl_assert(is_sane_TId(tid));
   frames = shadowStacks[tid];
   tl_assert(frames != NULL);
   nFrames = VG_(sizeXA)( frames );
   tl_assert(nFrames > 0);

   frame = VG_(indexXA)( frames, nFrames-1 );

   /* Find the instance info for this instruction. */
   tl_assert(ip_frameBlocks);
   iinstance = find_or_create_IInstance( frame, ip, ip_frameBlocks );
   tl_assert(iinstance);
   tl_assert(iinstance->blocks == ip_frameBlocks);

   szB = (sszB < 0) ? (-sszB) : sszB;
   tl_assert(szB > 0);

   inv = &iinstance->invar;

   /* Deal with first uses of instruction instances.  We hope this is
      rare, because it's expensive. */
   if (inv->tag == Inv_Unset) {
      /* This is the first use of this instance of the instruction, so
         we can't make any check; we merely record what we saw, so we
         can compare it against what happens for 2nd and subsequent
         accesses. */
      classify_address( inv,
                        tid, ea, sp, fp, szB,
                        iinstance->blocks, frames );
      tl_assert(inv->tag != Inv_Unset);
      return;
   }

   /* So generate an Invar and see if it's different from what
      we had before. */
   classify_address( &new_inv,
                     tid, ea, sp, fp, szB,
                     iinstance->blocks, frames );
   tl_assert(new_inv.tag != Inv_Unset);

   /* Did we see something different from before?  If no, then there's
      no error. */
   if (eq_Invar(&new_inv, inv))
      return;

   VG_(message)(Vg_UserMsg, "");
   VG_(message)(Vg_UserMsg, "Invalid %s of size %lu", 
                            sszB < 0 ? "write" : "read", szB );
   VG_(pp_ExeContext)(
      VG_(record_ExeContext)( tid, 0/*first_ip_delta*/ ) );
      // VG_(record_depth_1_ExeContext)( tid ) );

   VG_(message)(Vg_UserMsg, " Address %#lx expected vs actual:", ea);

   VG_(memset)(buf, 0, sizeof(buf));
   show_Invar( buf, sizeof(buf)-1, inv, nFrames );
   VG_(message)(Vg_UserMsg, " Expected: %s", buf );

   VG_(memset)(buf, 0, sizeof(buf));
   show_Invar( buf, sizeof(buf)-1, &new_inv, nFrames );
   VG_(message)(Vg_UserMsg, " Actual:   %s", buf );

   /* And now install the new observation as "standard", so as to
      make future error messages make more sense. */
   *inv = new_inv;
}


////////////////////////////////////////
/* Primary push-a-new-frame routine.  Called indirectly from
   generated code. */

static
void shadowStack_new_frame ( ThreadId tid,
                             Addr     sp_at_call_insn,
                             Addr     sp_post_call_insn,
                             Addr     fp_at_call_insn,
                             Addr     ip_post_call_insn,
                             XArray*  descrs_at_call_insn )
{
   Word n;
   StackFrame callee, *caller;
   tl_assert(is_sane_TId(tid));
   tl_assert(shadowStacks[tid] != NULL);

   n = VG_(sizeXA)( shadowStacks[tid] );
   tl_assert(n > 0);

   if (n > 1)
      tl_assert(descrs_at_call_insn);

   caller = VG_(indexXA)( shadowStacks[tid], n-1 );

   caller->sp_at_call = sp_at_call_insn;
   caller->fp_at_call = fp_at_call_insn;

   if (descrs_at_call_insn) {
      caller->blocks_added_by_call
         = calculate_StackBlock_EAs( descrs_at_call_insn,
                                     sp_at_call_insn, fp_at_call_insn );
      add_blocks_to_StackTree( siTrees[tid], 
                               descrs_at_call_insn,
                               caller->blocks_added_by_call,
                               n-1 /* stack depth at which these
                                      blocks are considered to exist*/ );
   } else {
      caller->blocks_added_by_call = NULL;
   }

   /* caller->blocks_added_by_call is used again (and then freed) when
      this frame is removed from the stack. */

   /* This sets up .htab, .htab_size and .htab_used */
   initialise_hash_table( &callee );

   callee.creation_sp    = sp_post_call_insn;
   callee.sp_at_call     = 0; // not actually required ..
   callee.fp_at_call     = 0; // .. these 3 initialisations are ..
   callee.blocks_added_by_call = NULL; // .. just for cleanness

   VG_(addToXA)( shadowStacks[tid], &callee );

   if (0)
   { Word d = VG_(sizeXA)( shadowStacks[tid] );
     HChar fnname[80];
     Bool ok;
     Addr ip = ip_post_call_insn;
     ok = VG_(get_fnname_w_offset)( ip, fnname, sizeof(fnname) );
     while (d > 0) {
        VG_(printf)(" ");
        d--;
     }
     VG_(printf)("> %s %#lx\n", ok ? fnname : "???", ip);
   }
}

/* CALLED FROM GENERATED CODE */
static
VG_REGPARM(3)
void helperc__new_frame ( Addr sp_post_call_insn,
                          Addr fp_at_call_insn,
                          Addr ip_post_call_insn,
                          XArray* blocks_at_call_insn,
                          Word sp_adjust )
{
   ThreadId tid = VG_(get_running_tid)();
   Addr     sp_at_call_insn = sp_post_call_insn + sp_adjust;
   shadowStack_new_frame( tid,
                          sp_at_call_insn,
                          sp_post_call_insn,
                          fp_at_call_insn,
                          ip_post_call_insn,
                          blocks_at_call_insn );
}


////////////////////////////////////////
/* Primary remove-frame(s) routine.  Called indirectly from
   generated code. */

static void shadowStack_unwind ( ThreadId tid, Addr sp_now )
{
   StackFrame* innermost;
   tl_assert(is_sane_TId(tid));
   tl_assert(shadowStacks[tid] != NULL);
   //VG_(printf)("UNWIND sp_new = %p\n", sp_now);
   while (1) {
      Word nFrames = VG_(sizeXA)( shadowStacks[tid] );
      tl_assert(nFrames >= 0);
      if (nFrames == 0) break;
      innermost = VG_(indexXA)( shadowStacks[tid], nFrames-1 );
      tl_assert(innermost->blocks_added_by_call == NULL);
      if (sp_now <= innermost->creation_sp) break;
      //VG_(printf)("UNWIND     dump %p\n", innermost->creation_sp);
      tl_assert(innermost->htab);
      pc_free(innermost->htab);
      /* be on the safe side */
      innermost->creation_sp = 0;
      innermost->htab = NULL;
      innermost->htab_size = 0;
      innermost->htab_used = 0;
      innermost->sp_at_call = 0;
      innermost->fp_at_call = 0;
      innermost->blocks_added_by_call = NULL;
      VG_(dropTailXA)( shadowStacks[tid], 1 );

      /* So now we're "back" in the calling frame.  Remove from this
         thread's stack-interval-tree, the blocks added at the time of
         the call. */
      nFrames = VG_(sizeXA)( shadowStacks[tid] );
      if (nFrames > 0) {
         innermost = VG_(indexXA)( shadowStacks[tid], nFrames-1 );
         tl_assert(innermost->blocks_added_by_call != NULL);
         del_blocks_from_StackTree( siTrees[tid],
                                    innermost->blocks_added_by_call );
         innermost->blocks_added_by_call = NULL;
      }
      /* That completes the required tidying of the interval tree
         associated with the frame we just removed. */

      if (0) {
         Word d = nFrames;
         while (d > 0) {
            VG_(printf)(" ");
            d--;
         }
         VG_(printf)("X\n");
      }

   }
}



//////////////////////////////////////////////////////////////
//                                                          //
// Instrumentation                                          //
//                                                          //
//////////////////////////////////////////////////////////////

/* What does instrumentation need to do?

   - at each Call transfer, generate a call to shadowStack_new_frame
     do this by manually inspecting the IR

   - at each sp change, if the sp change is negative, 
     call shadowStack_unwind
     do this by asking for SP-change analysis

   - for each memory referencing instruction,
     call helperc__mem_access
*/

static IRTemp gen_Get_SP ( IRSB*           bbOut,
                           VexGuestLayout* layout,
                           Int             hWordTy_szB )
{
   IRExpr* sp_expr;
   IRTemp  sp_temp;
   IRType  sp_type;
   /* This in effect forces the host and guest word sizes to be the
      same. */
   tl_assert(hWordTy_szB == layout->sizeof_SP);
   sp_type = layout->sizeof_SP == 8 ? Ity_I64 : Ity_I32;
   sp_expr = IRExpr_Get( layout->offset_SP, sp_type );
   sp_temp = newIRTemp( bbOut->tyenv, sp_type );
   addStmtToIRSB( bbOut, IRStmt_WrTmp( sp_temp, sp_expr ) );
   return sp_temp;
}

static IRTemp gen_Get_FP ( IRSB*           bbOut,
                           VexGuestLayout* layout,
                           Int             hWordTy_szB )
{
   IRExpr* fp_expr;
   IRTemp  fp_temp;
   IRType  fp_type;
   /* This in effect forces the host and guest word sizes to be the
      same. */
   tl_assert(hWordTy_szB == layout->sizeof_SP);
   fp_type = layout->sizeof_FP == 8 ? Ity_I64 : Ity_I32;
   fp_expr = IRExpr_Get( layout->offset_FP, fp_type );
   fp_temp = newIRTemp( bbOut->tyenv, fp_type );
   addStmtToIRSB( bbOut, IRStmt_WrTmp( fp_temp, fp_expr ) );
   return fp_temp;
}

static void instrument_mem_access ( IRSB*   bbOut, 
                                    IRExpr* addr,
                                    Int     szB,
                                    Bool    isStore,
                                    Int     hWordTy_szB,
                                    Addr    curr_IP,
                                    VexGuestLayout* layout )
{
   IRType  tyAddr      = Ity_INVALID;
   XArray* frameBlocks = NULL;

   tl_assert(isIRAtom(addr));
   tl_assert(hWordTy_szB == 4 || hWordTy_szB == 8);

   tyAddr = typeOfIRExpr( bbOut->tyenv, addr );
   tl_assert(tyAddr == Ity_I32 || tyAddr == Ity_I64);

#if defined(VGA_x86)
   { UChar* p = (UChar*)curr_IP;
     // pop %ebp; RET
     if (p[-1] == 0x5d && p[0] == 0xc3) return;
     // pop %ebp; RET $imm16
     if (p[-1] == 0x5d && p[0] == 0xc2) return;
     // PUSH %EBP; mov %esp,%ebp
     if (p[0] == 0x55 && p[1] == 0x89 && p[2] == 0xe5) return;
   }
#endif

   /* First off, find or create the StackBlocks for this instruction. */
   frameBlocks = get_StackBlocks_for_IP( curr_IP );
   tl_assert(frameBlocks);

   /* Generate a call to "helperc__mem_access", passing:
         addr current_SP current_FP szB curr_IP frameBlocks
   */
   { IRTemp t_SP = gen_Get_SP( bbOut, layout, hWordTy_szB );
     IRTemp t_FP = gen_Get_FP( bbOut, layout, hWordTy_szB );
     IRExpr** args
        = mkIRExprVec_6( addr,
                         IRExpr_RdTmp(t_SP),
                         IRExpr_RdTmp(t_FP),
                         mkIRExpr_HWord( isStore ? (-szB) : szB ),
                         mkIRExpr_HWord( curr_IP ),
                         mkIRExpr_HWord( (HWord)frameBlocks ) );
     IRDirty* di
        = unsafeIRDirty_0_N( 3/*regparms*/, 
                             "helperc__mem_access", 
                             VG_(fnptr_to_fnentry)( &helperc__mem_access ),
                             args );

     addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
   }
}


static
IRSB* di_instrument ( VgCallbackClosure* closure,
                      IRSB* sbIn,
                      VexGuestLayout* layout,
                      VexGuestExtents* vge,
                      IRType gWordTy, IRType hWordTy )
{
   Int   i;
   IRSB* sbOut;

   Addr curr_IP       = 0;
   Bool curr_IP_known = False;

   Bool firstRef = True;

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   /* Set up BB */
   sbOut           = emptyIRSB();
   sbOut->tyenv    = deepCopyIRTypeEnv(sbIn->tyenv);
   sbOut->next     = deepCopyIRExpr(sbIn->next);
   sbOut->jumpkind = sbIn->jumpkind;

   // Copy verbatim any IR preamble preceding the first IMark
   i = 0;
   while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
      addStmtToIRSB( sbOut, sbIn->stmts[i] );
      i++;
   }

   for (/*use current i*/; i < sbIn->stmts_used; i++) {
      IRStmt* st = sbIn->stmts[i];
      tl_assert(st);
      tl_assert(isFlatIRStmt(st));
      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_MBE:
            /* None of these can contain any memory references. */
            break;

         case Ist_Exit:
            tl_assert(st->Ist.Exit.jk != Ijk_Call);
            /* else we must deal with a conditional call */
            break;

         case Ist_IMark:
            curr_IP_known = True;
            curr_IP       = (Addr)st->Ist.IMark.addr;
            firstRef      = True;
            break;

         case Ist_Store:
            tl_assert(curr_IP_known);
            if (firstRef) {
            instrument_mem_access( 
               sbOut, 
               st->Ist.Store.addr, 
               sizeofIRType(typeOfIRExpr(sbIn->tyenv, st->Ist.Store.data)),
               True/*isStore*/,
               sizeofIRType(hWordTy),
               curr_IP, layout
            );
            firstRef = False;
            }
            break;

         case Ist_WrTmp: {
            IRExpr* data = st->Ist.WrTmp.data;
            if (data->tag == Iex_Load) {
               tl_assert(curr_IP_known);
               if (firstRef) {
               instrument_mem_access(
                  sbOut,
                  data->Iex.Load.addr,
                  sizeofIRType(data->Iex.Load.ty),
                  False/*!isStore*/,
                  sizeofIRType(hWordTy),
                  curr_IP, layout
               );
               firstRef = False;
               }
            }
            break;
         }

         case Ist_Dirty: {
            Int      dataSize;
            IRDirty* d = st->Ist.Dirty.details;
            if (d->mFx != Ifx_None) {
               /* This dirty helper accesses memory.  Collect the
                  details. */
               tl_assert(curr_IP_known);
               if (firstRef) {
               tl_assert(d->mAddr != NULL);
               tl_assert(d->mSize != 0);
               dataSize = d->mSize;
               if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify) {
                  instrument_mem_access( 
                     sbOut, d->mAddr, dataSize, False/*!isStore*/,
                     sizeofIRType(hWordTy), curr_IP, layout
                  );
               }
               if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify) {
                  instrument_mem_access( 
                     sbOut, d->mAddr, dataSize, True/*isStore*/,
                     sizeofIRType(hWordTy), curr_IP, layout
                  );
               }
               firstRef = False;
               }
            } else {
               tl_assert(d->mAddr == NULL);
               tl_assert(d->mSize == 0);
            }
            break;
         }

         default:
            tl_assert(0);

      } /* switch (st->tag) */

      addStmtToIRSB( sbOut, st );
   } /* iterate over sbIn->stmts */

   if (sbIn->jumpkind == Ijk_Call) {
      // Assumes x86 or amd64
      IRTemp   sp_post_call_insn, fp_post_call_insn;
      XArray*  frameBlocks;
      IRExpr** args;
      IRDirty* di;
      sp_post_call_insn
         = gen_Get_SP( sbOut, layout, sizeofIRType(hWordTy) );
      fp_post_call_insn
         = gen_Get_FP( sbOut, layout, sizeofIRType(hWordTy) );
      tl_assert(curr_IP_known);
      frameBlocks = get_StackBlocks_for_IP( curr_IP );
      tl_assert(frameBlocks);
      args
         = mkIRExprVec_5(
              IRExpr_RdTmp(sp_post_call_insn),
              IRExpr_RdTmp(fp_post_call_insn), 
                         /* assume the call doesn't change FP */
              sbIn->next,
              mkIRExpr_HWord( (HWord)frameBlocks ),
              mkIRExpr_HWord( sizeofIRType(gWordTy) )
           );
      di = unsafeIRDirty_0_N(
              3/*regparms*/,
              "helperc__new_frame",
              VG_(fnptr_to_fnentry)( &helperc__new_frame ),
              args ); 
      addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
   }

   return sbOut;
}


//////////////////////////////////////////////////////////////
//                                                          //
// end Instrumentation                                      //
//                                                          //
//////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////
//                                                          //
// misc                                                     //
//                                                          //
//////////////////////////////////////////////////////////////

/* Make a new shadow stack, with a creation_sp of effectively infinity,
   so that the top frame can never be removed. */
static XArray* /* of StackFrame */ new_empty_Stack ( void )
{
   StackFrame sframe;
   XArray* st = VG_(newXA)( pc_malloc, pc_free, sizeof(StackFrame) );
   VG_(memset)( &sframe, 0, sizeof(sframe) );
   sframe.creation_sp = ~0UL;

   /* This sets up .htab, .htab_size and .htab_used */
   initialise_hash_table( &sframe );

   VG_(addToXA)( st, &sframe );
   return st;
}

/* Primary routine for setting up the shadow stack for a new thread.
   Note that this is used to create not only child thread stacks, but
   the root thread's stack too.  We create a new stack with
   .creation_sp set to infinity, so that the outermost frame can never
   be removed (by shadowStack_unwind).  The core calls this function
   as soon as a thread is created.  We cannot yet get its SP value,
   since that may not yet be set. */
static void shadowStack_thread_create ( ThreadId parent, ThreadId child )
{
   tl_assert(is_sane_TId(child));
   if (parent == VG_INVALID_THREADID) {
      /* creating the main thread's stack */
   } else {
      tl_assert(0);
      tl_assert(shadowStacks[parent] != NULL);
   }
   if (shadowStacks[child] != NULL) {
      tl_assert(siTrees[child] != NULL);
      VG_(deleteXA)( shadowStacks[child] );
      delete_StackTree( siTrees[child] );
   } else {
      tl_assert(siTrees[child] == NULL);
   }
   shadowStacks[child] = new_empty_Stack();
   siTrees[child] = new_StackTree();
}

/* Once a thread is ready to go, the core calls here.  We take the
   opportunity to push a second frame on its stack, with the
   presumably valid SP value that is going to be used for the thread's
   startup.  Hence we should always wind up with a valid outermost
   frame for the thread. */
static void shadowStack_set_initial_SP ( ThreadId tid )
{
   StackFrame* sfp;
   tl_assert(is_sane_TId(tid));
   tl_assert(shadowStacks[tid] != NULL);
   tl_assert( VG_(sizeXA)(shadowStacks[tid]) == 1 );
   sfp = VG_(indexXA)( shadowStacks[tid], 0 );
   tl_assert(sfp->creation_sp == ~0UL);
   shadowStack_new_frame( tid, 0, VG_(get_SP)(tid),
                               0, VG_(get_IP)(tid), NULL );
}

/* CALLED indirectly FROM GENERATED CODE */
static void sg_die_mem_stack ( Addr old_SP, SizeT len ) {
   ThreadId  tid = VG_(get_running_tid)();
   shadowStack_unwind( tid, old_SP+len );
}


static void sg_post_clo_init(void)
{
}

static void sg_fini(Int exitcode)
{
  VG_(message)(Vg_DebugMsg,
     "%'llu total accesses, of which:", stats__total_accesses);
  VG_(message)(Vg_DebugMsg,
     "   stack0: %'12llu classify",
     stats__classify_Stack0);
  VG_(message)(Vg_DebugMsg,
     "   stackN: %'12llu classify",
     stats__classify_StackN);
  VG_(message)(Vg_DebugMsg,
     "   global: %'12llu classify",
     stats__classify_Global);
  VG_(message)(Vg_DebugMsg,
     "  unknown: %'12llu classify",
     stats__classify_Unknown);
  VG_(message)(Vg_DebugMsg,
     "%'llu Invars preened, of which %'llu changed",
     stats__Invars_preened, stats__Invars_changed);
  VG_(message)(Vg_DebugMsg, "");
}

static void sg_pre_clo_init(void)
{
   VG_(details_name)            ("SGcheck");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("a stack & global array overrun detector");
   VG_(details_copyright_author)(
      "Copyright (C) 2008-2008, and GNU GPL'd, by OpenWorks Ltd.");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);

   VG_(basic_tool_funcs)        (sg_post_clo_init,
                                 di_instrument,
                                 sg_fini);

   VG_(needs_var_info)            ();

   ourGlobals_init();
   init_StackBlocks_set();
   init_GlobalBlock_set();

   VG_(clo_vex_control).iropt_unroll_thresh = 0;
   VG_(clo_vex_control).guest_chase_thresh = 0;
   VG_(track_die_mem_stack) ( sg_die_mem_stack );
   VG_(track_pre_thread_ll_create)( shadowStack_thread_create );
   VG_(track_pre_thread_first_insn)( shadowStack_set_initial_SP );

   VG_(track_new_mem_mmap)         ( sg_new_mem_mmap );
   VG_(track_new_mem_startup) (sg_new_mem_startup);
   VG_(track_die_mem_munmap)( sg_die_mem_munmap );
}

VG_DETERMINE_INTERFACE_VERSION(sg_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                sg_main.c ---*/
/*--------------------------------------------------------------------*/
