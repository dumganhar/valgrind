
/*--------------------------------------------------------------------*/
/*--- Memory-related stuff: segment initialisation and tracking,   ---*/
/*--- stack operations                                             ---*/
/*---                                                  vg_memory.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, an extensible x86 protected-mode
   emulator for monitoring program execution on x86-Unixes.

   Copyright (C) 2000-2003 Julian Seward 
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

#include "vg_include.h"

#include <stddef.h>

/* Define to debug the memory-leak-detector. */
/* #define VG_DEBUG_LEAKCHECK */

static const Bool mem_debug = False;

static Int addrcmp(const void *ap, const void *bp)
{
   Addr a = *(Addr *)ap;
   Addr b = *(Addr *)bp;
   Int ret;

   if (a == b)
      ret = 0;
   else
      ret = (a < b) ? -1 : 1;

   return ret;
}

static Char *straddr(void *p)
{
   static Char buf[16];

   VG_(sprintf)(buf, "%p", *(Addr *)p);

   return buf;
}

static SkipList sk_segments = SKIPLIST_INIT(Segment, addr, addrcmp, straddr, VG_AR_CORE);

/*--------------------------------------------------------------*/
/*--- Maintain an ordered list of all the client's mappings  ---*/
/*--------------------------------------------------------------*/

Bool VG_(seg_contains)(const Segment *s, Addr p, UInt len)
{
   Addr se = s->addr+s->len;
   Addr pe = p+len;

   vg_assert(pe >= p);

   return (p >= s->addr && pe <= se);
}

Bool VG_(seg_overlaps)(const Segment *s, Addr p, UInt len)
{
   Addr se = s->addr+s->len;
   Addr pe = p+len;

   vg_assert(pe >= p);

   return (p < se && pe > s->addr);
}

/* Prepare a Segment structure for recycling by freeing everything
   hanging off it. */
static void recycleseg(Segment *s)
{
   if (s->flags & SF_CODE)
      VG_(invalidate_translations)(s->addr, s->len, False);

   if (s->filename != NULL)
      VG_(arena_free)(VG_AR_CORE, (Char *)s->filename);

   /* keep the SegInfo, if any - it probably still applies */
}

/* When freeing a Segment, also clean up every one else's ideas of
   what was going on in that range of memory */
static void freeseg(Segment *s)
{
   recycleseg(s);
   if (s->symtab != NULL) {
      VG_(symtab_decref)(s->symtab, s->addr, s->len);
      s->symtab = NULL;
   }

   VG_(SkipNode_Free)(&sk_segments, s);
}

/* Split a segment at address a */
static Segment *split_segment(Addr a)
{
   Segment *s = VG_(SkipList_Find)(&sk_segments, &a);
   Segment *ns;
   Int delta;

   vg_assert((a & (VKI_BYTES_PER_PAGE-1)) == 0);

   /* missed */
   if (s == NULL)
      return NULL;

   /* a at or beyond endpoint */
   if (s->addr == a || a >= (s->addr+s->len))
      return NULL;

   vg_assert(a > s->addr && a < (s->addr+s->len));

   ns = VG_(SkipNode_Alloc)(&sk_segments);

   *ns = *s;

   delta = a - s->addr;
   ns->addr += delta;
   ns->offset += delta;
   ns->len -= delta;

   if (ns->symtab != NULL)
      VG_(symtab_incref)(ns->symtab);

   VG_(SkipList_Insert)(&sk_segments, ns);

   return ns;
}

/* This unmaps all the segments in the range [addr, addr+len); any
   partial mappings at the ends are truncated. */
void VG_(unmap_range)(Addr addr, UInt len)
{
   Segment *s;
   Segment *next;
   static const Bool debug = False || mem_debug;

   if (len == 0)
      return;

   if (debug)
      VG_(printf)("unmap_range(%p, %d)\n", addr, len);

   len = PGROUNDUP(addr+len)-PGROUNDDN(addr);
   addr = PGROUNDDN(addr);

   /* Everything must be page-aligned */
   vg_assert((addr & (VKI_BYTES_PER_PAGE-1)) == 0);
   vg_assert((len  & (VKI_BYTES_PER_PAGE-1)) == 0);

   for(s = VG_(SkipList_Find)(&sk_segments, &addr); 
       s != NULL && s->addr < (addr+len); 
       s = next) {

      /* fetch next now in case we end up deleting this segment */
      next = VG_(SkipNode_Next)(&sk_segments, s);

      if (debug)
	 VG_(printf)("unmap: addr=%p s=%p ->addr=%p len=%d end=%p\n",
		     addr, s, s->addr, s->len, s->addr+s->len);

      if (!VG_(seg_overlaps)(s, addr, len))
	 continue;

      /* 4 cases: */
      if (addr > s->addr && addr < (s->addr + s->len)) {
	 /* this segment's tail is truncated by [addr, addr+len)
	    -> truncate tail
	 */
	 s->len = addr - s->addr;

	 if (debug)
	    VG_(printf)("  case 1: s->len=%d\n", s->len);
      } else if (addr <= s->addr && (addr+len) >= (s->addr + s->len)) {
	 /* this segment is completely contained within [addr, addr+len)
	    -> delete segment
	 */
	 Segment *rs = VG_(SkipList_Remove)(&sk_segments, &s->addr);
	 vg_assert(rs == s);
	 freeseg(s);

	 if (debug)
	    VG_(printf)("  case 2: s==%p deleted\n", s);
      } else if ((addr+len) > s->addr && (addr+len) < (s->addr+s->len)) {
	 /* this segment's head is truncated by [addr, addr+len)
	    -> truncate head
	 */
	 Int delta = (addr+len) - s->addr;

	 s->addr += delta;
	 s->offset += delta;
	 s->len -= delta;

	 if (debug)
	    VG_(printf)("  case 3: s->addr=%p s->len=%d delta=%d\n", s->addr, s->len, delta);
      } else if (addr > s->addr && (addr+len) < (s->addr + s->len)) {
	 /* [addr, addr+len) is contained within a single segment
	    -> split segment into 3, delete middle portion
	  */
	 Segment *middle, *rs;

	 middle = split_segment(addr);
	 split_segment(addr+len);

	 vg_assert(middle->addr == addr);
	 rs = VG_(SkipList_Remove)(&sk_segments, &addr);
	 vg_assert(rs == middle);

	 freeseg(rs);

	 if (debug)
	    VG_(printf)("  case 4: subrange %p-%p deleted\n",
			addr, addr+len);
      }
   }
}

/* If possible, merge segment with its neighbours - some segments,
   including s, may be destroyed in the process */
static inline Bool neighbours(Segment *s1, Segment *s2)
{
   if (s1->addr+s1->len != s2->addr)
      return False;

   if (s1->flags != s2->flags)
      return False;

   if (s1->prot != s2->prot)
      return False;

   if (s1->symtab != s2->symtab)
      return False;

   if (s1->flags & SF_FILE){
      if ((s1->offset + s1->len) != s2->offset)
	 return False;
      if (s1->dev != s2->dev)
	 return False;
      if (s1->ino != s2->ino)
	 return False;
   }
   
   return True;
}

/* Merge segments in the address range if they're adjacent and
   compatible */
static void merge_segments(Addr a, UInt len)
{
   Segment *s;
   Segment *next;

   vg_assert((a & (VKI_BYTES_PER_PAGE-1)) == 0);
   vg_assert((len & (VKI_BYTES_PER_PAGE-1)) == 0);

   a -= VKI_BYTES_PER_PAGE;
   len += VKI_BYTES_PER_PAGE;

   for(s = VG_(SkipList_Find)(&sk_segments, &a);
       s != NULL && s->addr < (a+len);) {
      next = VG_(SkipNode_Next)(&sk_segments, s);

      if (next && neighbours(s, next)) {
	 Segment *rs;

	 if (0)
	    VG_(printf)("merge %p-%p with %p-%p\n",
			s->addr, s->addr+s->len,
			next->addr, next->addr+next->len);
	 s->len += next->len;
	 s = VG_(SkipNode_Next)(&sk_segments, next);

	 rs = VG_(SkipList_Remove)(&sk_segments, &next->addr);
	 vg_assert(next == rs);
	 freeseg(next);
      } else
	 s = next;
   }
}

void VG_(map_file_segment)(Addr addr, UInt len, UInt prot, UInt flags, 
			   UInt dev, UInt ino, ULong off, const Char *filename)
{
   Segment *s;
   static const Bool debug = False || mem_debug;
   Bool recycled;

   if (debug)
      VG_(printf)("map_file_segment(%p, %d, %x, %x, %4x, %d, %ld, %s)\n",
		  addr, len, prot, flags, dev, ino, off, filename);

   /* Everything must be page-aligned */
   vg_assert((addr & (VKI_BYTES_PER_PAGE-1)) == 0);
   len = PGROUNDUP(len);

   /* First look to see what already exists around here */
   s = VG_(SkipList_Find)(&sk_segments, &addr);

   if (s != NULL && s->addr == addr && s->len == len) {
      /* This probably means we're just updating the flags */
      recycled = True;
      recycleseg(s);

      /* If we had a symtab, but the new mapping is incompatible, then
	 free up the old symtab in preparation for a new one. */
      if (s->symtab != NULL		&&
	  (!(s->flags & SF_FILE)	||
	   !(flags & SF_FILE)		||
	   s->dev != dev		||
	   s->ino != ino		||
	   s->offset != off)) {
	 VG_(symtab_decref)(s->symtab, s->addr, s->len);
	 s->symtab = NULL;
      }
   } else {
      recycled = False;
      VG_(unmap_range)(addr, len);

      s = VG_(SkipNode_Alloc)(&sk_segments);

      s->addr   = addr;
      s->len    = len;
      s->symtab = NULL;
   }

   s->flags  = flags;
   s->prot   = prot;
   s->dev    = dev;
   s->ino    = ino;
   s->offset = off;
   
   if (filename != NULL)
      s->filename = VG_(arena_strdup)(VG_AR_CORE, filename);
   else
      s->filename = NULL;

   if (debug) {
      Segment *ts;
      for(ts = VG_(SkipNode_First)(&sk_segments);
	  ts != NULL;
	  ts = VG_(SkipNode_Next)(&sk_segments, ts))
	 VG_(printf)("list: %8p->%8p ->%d (0x%x) prot=%x flags=%x\n",
		     ts, ts->addr, ts->len, ts->len, ts->prot, ts->flags);

      VG_(printf)("inserting s=%p addr=%p len=%d\n",
		  s, s->addr, s->len);
   }

   if (!recycled)
      VG_(SkipList_Insert)(&sk_segments, s);

   /* If this mapping is of the beginning of a file, isn't part of
      Valgrind, is at least readable and seems to contain an object
      file, then try reading symbols from it. */
   if ((flags & (SF_MMAP|SF_NOSYMS)) == SF_MMAP	&&
       s->symtab == NULL) {
      if (off == 0									&&
	  filename != NULL								&&
	  (prot & (VKI_PROT_READ|VKI_PROT_EXEC)) == (VKI_PROT_READ|VKI_PROT_EXEC)	&&
	  len >= VKI_BYTES_PER_PAGE							&&
	  s->symtab == NULL								&&
	  VG_(is_object_file)((void *)addr)) {

      s->symtab = VG_(read_seg_symbols)(s);

      if (s->symtab != NULL)
	 s->flags |= SF_DYNLIB;
      } else if (flags & SF_MMAP) {
	 const SegInfo *info;

	 /* Otherwise see if an existing symtab applies to this Segment */
	 for(info = VG_(next_seginfo)(NULL);
	     info != NULL;
	     info = VG_(next_seginfo)(info)) {
	    if (VG_(seg_overlaps)(s, VG_(seg_start)(info), VG_(seg_size)(info))) {
	       s->symtab = (SegInfo *)info;
	       VG_(symtab_incref)((SegInfo *)info);
	    }
	 }
      }
   }

   /* clean up */
   merge_segments(addr, len);
}

void VG_(map_fd_segment)(Addr addr, UInt len, UInt prot, UInt flags, 
			 Int fd, ULong off, const Char *filename)
{
   struct vki_stat st;
   Char *name = NULL;

   st.st_dev = 0;
   st.st_ino = 0;

   if (fd != -1 && (flags & SF_FILE)) {
      vg_assert((off & (VKI_BYTES_PER_PAGE-1)) == 0);

      if (VG_(fstat)(fd, &st) < 0)
	 flags &= ~SF_FILE;
   }

   if ((flags & SF_FILE) && filename == NULL && fd != -1)
      name = VG_(resolve_filename)(fd);

   if (filename == NULL)
      filename = name;

   VG_(map_file_segment)(addr, len, prot, flags, st.st_dev, st.st_ino, off, filename);

   if (name)
      VG_(arena_free)(VG_AR_CORE, name);
}

void VG_(map_segment)(Addr addr, UInt len, UInt prot, UInt flags)
{
   flags &= ~SF_FILE;

   VG_(map_file_segment)(addr, len, prot, flags, 0, 0, 0, 0);
}

/* set new protection flags on an address range */
void VG_(mprotect_range)(Addr a, UInt len, UInt prot)
{
   Segment *s, *next;
   static const Bool debug = False || mem_debug;

   if (debug)
      VG_(printf)("mprotect_range(%p, %d, %x)\n", a, len, prot);

   /* Everything must be page-aligned */
   vg_assert((a & (VKI_BYTES_PER_PAGE-1)) == 0);
   vg_assert((len & (VKI_BYTES_PER_PAGE-1)) == 0);

   split_segment(a);
   split_segment(a+len);

   for(s = VG_(SkipList_Find)(&sk_segments, &a);
       s != NULL && s->addr < a+len;
       s = next)
   {
      next = VG_(SkipNode_Next)(&sk_segments, s);
      if (s->addr < a)
	 continue;

      s->prot = prot;
   }

   merge_segments(a, len);
}

Addr VG_(find_map_space)(Addr addr, UInt len, Bool for_client)
{
   Segment *s;
   Addr ret;
   static const Bool debug = False || mem_debug;
   Addr limit = (for_client ? VG_(client_end) : VG_(valgrind_mmap_end));

   if (addr == 0)
      addr = for_client ? VG_(client_mapbase) : VG_(valgrind_base);
   else {
      /* leave space for redzone and still try to get the exact
	 address asked for */
      addr -= VKI_BYTES_PER_PAGE;
   }
   ret = addr;

   /* Everything must be page-aligned */
   vg_assert((addr & (VKI_BYTES_PER_PAGE-1)) == 0);
   len = PGROUNDUP(len);

   len += VKI_BYTES_PER_PAGE * 2; /* leave redzone gaps before and after mapping */

   if (debug)
      VG_(printf)("find_map_space: ret starts as %p-%p client=%d\n",
		  ret, ret+len, for_client);

   for(s = VG_(SkipList_Find)(&sk_segments, &ret);
       s != NULL && s->addr < (ret+len);
       s = VG_(SkipNode_Next)(&sk_segments, s))
   {
      if (debug)
	 VG_(printf)("s->addr=%p len=%d (%p) ret=%p\n",
		     s->addr, s->len, s->addr+s->len, ret);

      if (s->addr < (ret + len) && (s->addr + s->len) > ret)
	 ret = s->addr+s->len;
   }

   if (debug) {
      if (s)
	 VG_(printf)("  s->addr=%p ->len=%d\n", s->addr, s->len);
      else
	 VG_(printf)("  s == NULL\n");
   }

   if ((limit - len) < ret)
      ret = 0;			/* no space */
   else
      ret += VKI_BYTES_PER_PAGE; /* skip leading redzone */

   if (debug)
      VG_(printf)("find_map_space(%p, %d, %d) -> %p\n",
		  addr, len, for_client, ret);
   
   return ret;
}

Segment *VG_(find_segment)(Addr a)
{
   return VG_(SkipList_Find)(&sk_segments, &a);
}

Segment *VG_(next_segment)(Segment *s)
{
   return VG_(SkipNode_Next)(&sk_segments, s);
}

/*--------------------------------------------------------------*/
/*--- Initialise program data/text etc on program startup.   ---*/
/*--------------------------------------------------------------*/

static
void build_valgrind_map_callback ( Addr start, UInt size, 
				   Char rr, Char ww, Char xx, UInt dev, UInt ino,
				   ULong foffset, const UChar* filename )
{
   UInt prot = 0;
   UInt flags;
   Bool is_stack_segment;
   Bool verbose = False || mem_debug; /* set to True for debugging */

   is_stack_segment = (start == VG_(clstk_base) && (start+size) == VG_(clstk_end));

   prot = 0;
   flags = SF_MMAP|SF_NOSYMS;

   if (start >= VG_(valgrind_base) && (start+size) <= VG_(valgrind_end))
      flags |= SF_VALGRIND;

   /* Only record valgrind mappings for now, without loading any
      symbols.  This is so we know where the free space is before we
      start allocating more memory (note: heap is OK, it's just mmap
      which is the problem here). */
   if (flags & SF_VALGRIND) {
      if (verbose)
	 VG_(printf)("adding segment %08p-%08p prot=%x flags=%4x filename=%s\n",
		     start, start+size, prot, flags, filename);

      VG_(map_file_segment)(start, size, prot, flags, dev, ino, foffset, filename);
   }
}

static
void build_segment_map_callback ( Addr start, UInt size, 
				  Char rr, Char ww, Char xx, UInt dev, UInt ino,
				  ULong foffset, const UChar* filename )
{
   UInt prot = 0;
   UInt flags;
   Bool is_stack_segment;
   Bool verbose = False || mem_debug; /* set to True for debugging */
   Addr r_esp;

   is_stack_segment = (start == VG_(clstk_base) && (start+size) == VG_(clstk_end));

   if (rr == 'r')
      prot |= VKI_PROT_READ;
   if (ww == 'w')
      prot |= VKI_PROT_WRITE;
   if (xx == 'x')
      prot |= VKI_PROT_EXEC;

      
   if (is_stack_segment)
      flags = SF_STACK | SF_GROWDOWN;
   else
      flags = SF_EXEC|SF_MMAP;

   if (filename != NULL)
      flags |= SF_FILE;

   if (start >= VG_(valgrind_base) && (start+size) <= VG_(valgrind_end))
      flags |= SF_VALGRIND;

   if (verbose)
      VG_(printf)("adding segment %08p-%08p prot=%x flags=%4x filename=%s\n",
		  start, start+size, prot, flags, filename);

   VG_(map_file_segment)(start, size, prot, flags, dev, ino, foffset, filename);

   if (VG_(is_client_addr)(start) && VG_(is_client_addr)(start+size-1))
      VG_TRACK( new_mem_startup, start, size, rr=='r', ww=='w', xx=='x' );

   /* If this is the stack segment mark all below %esp as noaccess. */
   r_esp = VG_(m_state_static)[40/4];
   if (is_stack_segment) {
      if (0)
         VG_(message)(Vg_DebugMsg, "invalidating stack area: %x .. %x",
                      start,r_esp);
      VG_TRACK( die_mem_stack, start, r_esp-start );
   }
}


/* 1. Records startup segments from /proc/pid/maps.  Takes special note
      of the executable ones, because if they're munmap()ed we need to
      discard translations.  Also checks there's no exe segment overlaps.

      Note that `read_from_file' is false;  we read /proc/self/maps into a
      buffer at the start of VG_(main) so that any superblocks mmap'd by
      calls to VG_(malloc)() by SK_({pre,post}_clo_init) aren't erroneously
      thought of as being owned by the client.
 */
void VG_(init_memory) ( void )
{
   /* 1 */
   /* reserve Valgrind's kickstart, heap and stack */
   VG_(map_segment)(VG_(valgrind_mmap_end), VG_(valgrind_end)-VG_(valgrind_mmap_end),
		    VKI_PROT_NONE, SF_VALGRIND|SF_FIXED);

   /* work out what's mapped where, and read interesting symtabs */
   VG_(parse_procselfmaps) ( build_valgrind_map_callback );	/* just Valgrind mappings */
   VG_(parse_procselfmaps) ( build_segment_map_callback );	/* everything */

   /* kludge: some newer kernels place a "sysinfo" page up high, with
      vsyscalls in it, and possibly some other stuff in the future. */
   if (VG_(sysinfo_page_exists)) {
      // 2003-Sep-25, njn: Jeremy thinks the sysinfo page probably doesn't
      // have any symbols that need to be loaded.  So just treat it like
      // a non-executable page.
      //VG_(new_exeseg_mmap)( VG_(sysinfo_page_addr), 4096 );
      VG_TRACK( new_mem_startup, VG_(sysinfo_page_addr), 4096, 
                True, True, True );
     }
}

/*------------------------------------------------------------*/
/*--- Tracking permissions around %esp changes.            ---*/
/*------------------------------------------------------------*/

/*
   The stack
   ~~~~~~~~~
   The stack's segment seems to be dynamically extended downwards
   by the kernel as the stack pointer moves down.  Initially, a
   1-page (4k) stack is allocated.  When %esp moves below that for
   the first time, presumably a page fault occurs.  The kernel
   detects that the faulting address is in the range from %esp upwards
   to the current valid stack.  It then extends the stack segment
   downwards for enough to cover the faulting address, and resumes
   the process (invisibly).  The process is unaware of any of this.

   That means that Valgrind can't spot when the stack segment is
   being extended.  Fortunately, we want to precisely and continuously
   update stack permissions around %esp, so we need to spot all
   writes to %esp anyway.

   The deal is: when %esp is assigned a lower value, the stack is
   being extended.  Create a secondary maps to fill in any holes
   between the old stack ptr and this one, if necessary.  Then 
   mark all bytes in the area just "uncovered" by this %esp change
   as write-only.

   When %esp goes back up, mark the area receded over as unreadable
   and unwritable.

   Just to record the %esp boundary conditions somewhere convenient:
   %esp always points to the lowest live byte in the stack.  All
   addresses below %esp are not live; those at and above it are.  
*/

/* Kludgey ... how much does %esp have to change before we reckon that
   the application is switching stacks ? */
#define VG_PLAUSIBLE_STACK_SIZE  8000000
#define VG_HUGE_DELTA            (VG_PLAUSIBLE_STACK_SIZE / 4)

/* This function gets called if new_mem_stack and/or die_mem_stack are
   tracked by the skin, and one of the specialised cases (eg. new_mem_stack_4)
   isn't used in preference */
__attribute__((regparm(1)))
void VG_(unknown_esp_update)(Addr new_ESP)
{
   Addr old_ESP = VG_(get_archreg)(R_ESP);
   Int  delta   = (Int)new_ESP - (Int)old_ESP;

   if (delta < -(VG_HUGE_DELTA) || VG_HUGE_DELTA < delta) {
      /* %esp has changed by more than HUGE_DELTA.  We take this to mean
         that the application is switching to a new stack, for whatever
         reason. 
       
         JRS 20021001: following discussions with John Regehr, if a stack
         switch happens, it seems best not to mess at all with memory
         permissions.  Seems to work well with Netscape 4.X.  Really the
         only remaining difficulty is knowing exactly when a stack switch is
         happening. */
      if (VG_(clo_verbosity) > 1)
           VG_(message)(Vg_UserMsg, "Warning: client switching stacks?  "
                                    "%%esp: %p --> %p", old_ESP, new_ESP);
   } else if (delta < 0) {
      VG_TRACK( new_mem_stack, new_ESP, -delta );

   } else if (delta > 0) {
      VG_TRACK( die_mem_stack, old_ESP,  delta );
   }
}

static jmp_buf segv_jmpbuf;

static void segv_handler(Int seg)
{
   __builtin_longjmp(segv_jmpbuf, 1);
   VG_(core_panic)("longjmp failed");
}

/* 
   Test if a piece of memory is addressable by setting up a temporary
   SIGSEGV handler, then try to touch the memory.  No signal = good,
   signal = bad.
 */
Bool VG_(is_addressable)(Addr p, Int size)
{
   volatile Char * volatile cp = (volatile Char *)p;
   volatile Bool ret;
   vki_ksigaction sa, origsa;
   vki_ksigset_t mask;

   vg_assert(size > 0);

   sa.ksa_handler = segv_handler;
   sa.ksa_flags = 0;
   VG_(ksigfillset)(&sa.ksa_mask);
   VG_(ksigaction)(VKI_SIGSEGV, &sa, &origsa);
   VG_(ksigprocmask)(VKI_SIG_SETMASK, NULL, &mask);

   if (__builtin_setjmp(&segv_jmpbuf) == 0) {
      while(size--)
	 *cp++;
      ret = True;
    } else
      ret = False;

   VG_(ksigaction)(VKI_SIGSEGV, &origsa, NULL);
   VG_(ksigprocmask)(VKI_SIG_SETMASK, &mask, NULL);

   return ret;
}

/*--------------------------------------------------------------------*/
/*--- manage allocation of memory on behalf of the client          ---*/
/*--------------------------------------------------------------------*/

Addr VG_(client_alloc)(Addr addr, UInt len, UInt prot, UInt flags)
{
   len = PGROUNDUP(len);

   if (!(flags & SF_FIXED))
      addr = VG_(find_map_space)(addr, len, True);

   flags |= SF_CORE;

   if (VG_(mmap)((void *)addr, len, prot,
		 VKI_MAP_FIXED | VKI_MAP_PRIVATE | VKI_MAP_ANONYMOUS | VKI_MAP_CLIENT,
		 -1, 0) == (void *)addr) {
      VG_(map_segment)(addr, len, prot, flags);
      return addr;
   }

   return 0;
}

void VG_(client_free)(Addr addr)
{
   Segment *s = VG_(find_segment)(addr);

   if (s == NULL || s->addr != addr || !(s->flags & SF_CORE)) {
      VG_(message)(Vg_DebugMsg, "VG_(client_free)(%p) - no CORE memory found there", addr);
      return;
   }

   VG_(munmap)((void *)s->addr, s->len);
}

Bool VG_(is_client_addr)(Addr a)
{
   return a >= VG_(client_base) && a < VG_(client_end);
}

Bool VG_(is_shadow_addr)(Addr a)
{
   return a >= VG_(shadow_base) && a < VG_(shadow_end);
}

Bool VG_(is_valgrind_addr)(Addr a)
{
   return a >= VG_(valgrind_base) && a < VG_(valgrind_end);
}

Addr VG_(get_client_base)(void)
{
   return VG_(client_base);
}

Addr VG_(get_client_end)(void)
{
   return VG_(client_end);
}

Addr VG_(get_client_size)(void)
{
   return VG_(client_end)-VG_(client_base);
}

Addr VG_(get_shadow_base)(void)
{
   return VG_(shadow_base);
}

Addr VG_(get_shadow_end)(void)
{
   return VG_(shadow_end);
}

Addr VG_(get_shadow_size)(void)
{
   return VG_(shadow_end)-VG_(shadow_base);
}


void VG_(init_shadow_range)(Addr p, UInt sz, Bool call_init)
{
   if (0)
      VG_(printf)("init_shadow_range(%p, %d)\n", p, sz);

   vg_assert(VG_(needs).shadow_memory);
   vg_assert(VG_(defined_init_shadow_page)());

   sz = PGROUNDUP(p+sz) - PGROUNDDN(p);
   p = PGROUNDDN(p);

   VG_(mprotect)((void *)p, sz, VKI_PROT_READ|VKI_PROT_WRITE);
   
   if (call_init) 
      while(sz) {
	 /* ask the skin to initialize each page */
	 VG_TRACK( init_shadow_page, PGROUNDDN(p) );
	 
	 p += VKI_BYTES_PER_PAGE;
	 sz -= VKI_BYTES_PER_PAGE;
      }
}

void *VG_(shadow_alloc)(UInt size)
{
   static Addr shadow_alloc = 0;
   void *ret;

   vg_assert(VG_(needs).shadow_memory);
   vg_assert(!VG_(defined_init_shadow_page)());

   size = PGROUNDUP(size);

   if (shadow_alloc == 0)
      shadow_alloc = VG_(shadow_base);

   if (shadow_alloc >= VG_(shadow_end))
       return 0;

   ret = (void *)shadow_alloc;
   VG_(mprotect)(ret, size, VKI_PROT_READ|VKI_PROT_WRITE);

   shadow_alloc += size;

   return ret;
}

/*--------------------------------------------------------------------*/
/*--- end                                              vg_memory.c ---*/
/*--------------------------------------------------------------------*/

