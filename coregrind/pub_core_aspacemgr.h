
/*--------------------------------------------------------------------*/
/*--- The address space manager.              pub_core_aspacemgr.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2005 Julian Seward
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

#ifndef __PUB_CORE_ASPACEMGR_H
#define __PUB_CORE_ASPACEMGR_H

//--------------------------------------------------------------------
// PURPOSE: This module deals with management of the entire process
// address space.  Almost everything depends upon it, including dynamic
// memory management.  Hence this module is almost completely
// standalone; the only module it uses is m_debuglog.  DO NOT CHANGE
// THIS.
// [XXX: actually, this is far from true... especially that to #include
// this header, you have to #include pub_core_debuginfo in order to 
// see the SegInfo type, which is very bad...]
//--------------------------------------------------------------------

#include "pub_tool_aspacemgr.h"

// Address space globals
extern Addr VG_(client_base);	 // client address space limits
extern Addr VG_(client_end);
extern Addr VG_(client_mapbase); // base of mappings
extern Addr VG_(clstk_base);	 // client stack range
extern Addr VG_(clstk_end);
extern UWord VG_(clstk_id);      // client stack id

extern Addr VG_(brk_base);	 // start of brk
extern Addr VG_(brk_limit);	 // current brk
extern Addr VG_(shadow_base);	 // tool's shadow memory
extern Addr VG_(shadow_end);
extern Addr VG_(valgrind_base);	 // valgrind's address range
extern Addr VG_(valgrind_last);  // Nb: last byte, rather than one past the end

// Direct access to these system calls.
extern SysRes VG_(mmap_native)     ( void* start, SizeT length, UInt prot,
                                     UInt flags, UInt fd, OffT offset );
extern SysRes VG_(munmap_native)   ( void* start, SizeT length );
extern SysRes VG_(mprotect_native) ( void *start, SizeT length, UInt prot );

/* A Segment is mapped piece of client memory.  This covers all kinds
   of mapped memory (exe, brk, mmap, .so, shm, stack, etc)

   We encode relevant info about each segment with these constants.
*/
#define SF_SHARED   (1 <<  0) // shared
#define SF_SHM      (1 <<  1) // SYSV SHM (also SF_SHARED)
#define SF_MMAP     (1 <<  2) // mmap memory
#define SF_FILE     (1 <<  3) // mapping is backed by a file
#define SF_STACK    (1 <<  4) // is a stack
#define SF_GROWDOWN (1 <<  5) // segment grows down
#define SF_NOSYMS   (1 <<  6) // don't load syms, even if present
#define SF_CORE     (1 <<  7) // allocated by core on behalf of the client
#define SF_VALGRIND (1 <<  8) // a valgrind-internal mapping - not in client
#define SF_CODE     (1 <<  9) // segment contains cached code

typedef struct _Segment Segment;

struct _Segment {
   UInt         prot;         // VKI_PROT_*
   UInt         flags;        // SF_*

   Addr         addr;         // mapped addr (page aligned)
   SizeT        len;          // size of mapping (page aligned)

   // These are valid if (flags & SF_FILE)
   OffT        offset;        // file offset
   const Char* filename;      // filename (NULL if unknown)
   Int         fnIdx;         // filename table index (-1 if unknown)
   UInt        dev;           // device
   UInt        ino;           // inode

   SegInfo*    seginfo;       // symbol table, etc
};

/* segment mapped from a file descriptor */
extern void VG_(map_fd_segment)  (Addr addr, SizeT len, UInt prot, UInt flags, 
				  Int fd, ULong off, const Char *filename);

/* segment mapped from a file */
extern void VG_(map_file_segment)(Addr addr, SizeT len, UInt prot, UInt flags, 
				  UInt dev, UInt ino, ULong off, const Char *filename);

/* simple segment */
extern void VG_(map_segment)     (Addr addr, SizeT len, UInt prot, UInt flags);

extern void VG_(unmap_range)   (Addr addr, SizeT len);
extern void VG_(mprotect_range)(Addr addr, SizeT len, UInt prot);
extern Addr VG_(find_map_space)(Addr base, SizeT len, Bool for_client);

/* Find the segment containing a, or NULL if none. */
extern Segment *VG_(find_segment)(Addr a);

/* a is an unmapped address (is checked).  Find the next segment 
   along in the address space, or NULL if none. */
extern Segment *VG_(find_segment_above_unmapped)(Addr a);

/* a is a mapped address (in a segment, is checked).  Find the
   next segment along. */
extern Segment *VG_(find_segment_above_mapped)(Addr a);

extern Bool VG_(seg_contains)(const Segment *s, Addr ptr, SizeT size);
extern Bool VG_(seg_overlaps)(const Segment *s, Addr ptr, SizeT size);

extern Segment *VG_(split_segment)(Addr a);

extern void VG_(pad_address_space)  (Addr start);
extern void VG_(unpad_address_space)(Addr start);

///* Search /proc/self/maps for changes which aren't reflected in the
//   segment list */
//extern void VG_(sync_segments)(UInt flags);

/* Return string for prot */
extern const HChar *VG_(prot_str)(UInt prot);

/* Parses /proc/self/maps, calling `record_mapping' for each entry. */
extern 
void VG_(parse_procselfmaps) (
   void (*record_mapping)( Addr addr, SizeT len, UInt prot,
			   UInt dev, UInt ino, ULong foff,
                           const UChar *filename ) );

// Pointercheck
extern Bool VG_(setup_pointercheck) ( Addr client_base, Addr client_end );

/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////

#define VG_STACK_GUARD_SZB  8192
#define VG_STACK_ACTIVE_SZB 65536

typedef
   HChar
   VgStack[VG_STACK_GUARD_SZB + VG_STACK_ACTIVE_SZB + VG_STACK_GUARD_SZB];

typedef
   enum {
      SkFree,  // unmapped space
      SkAnon,  // anonymous mapping
      SkFile,  // mapping to a file
      SkResvn  // reservation
   }
   MKind;

typedef
   enum {
      SmLower,  // lower end can move up
      SmFixed,  // cannot be shrunk
      SmUpper   // upper end can move down
   }
   ShrinkMode;

/* Describes a segment.  Invariants:

     kind == SkFree:
        // the only meaningful fields are .start and .end

     kind == SkAnon:
        // the segment may be resized if required
        // there's no associated file:
        dev==ino==foff = 0, fnidx == -1
        // segment may have permissions

     kind == SkFile
        // the segment may not be resized:
        moveLo == moveHi == NotMovable, maxlen == 0
        // there is an associated file
        // segment may have permissions

     kind == SkResvn
        // the segment may be resized if required
        // there's no associated file:
        dev==ino==foff = 0, fnidx == -1
        // segment has no permissions
        hasR==hasW==hasX==anyTranslated == False

     Also: if !isClient then anyTranslated==False
           (viz, not allowed to make translations from non-client areas)
*/
typedef
   struct {
      MKind   kind;
      Bool    isClient;
      /* Extent (SkFree, SkAnon, SkFile, SkResvn) */
      Addr    start;    // lowest address in range
      Addr    end;      // highest address in range
      /* Shrinkable? (SkResvn only) */
      ShrinkMode smode;
      /* Associated file (SkFile only) */
      UInt    dev;
      UInt    ino;
      ULong   offset;
      Int     fnIdx;    // file name table index, if name is known
      /* Permissions (SkAnon, SkFile only) */
      Bool    hasR;
      Bool    hasW;
      Bool    hasX;
      Bool    hasT;     // True --> translations have (or MAY have)
      /* Admin */       // been taken from this segment
      Bool    mark;
   }
   NSegment;


/* Takes a pointer to the SP at the time V gained control.  This is
   taken to be the highest usable address (more or less).  Based on
   that (and general consultation of tea leaves, etc) return a
   suggested end address for the client's stack. */
extern Addr VG_(new_aspacem_start) ( Addr sp_at_startup );

extern void VG_(show_nsegments) ( Int logLevel, HChar* who );

typedef
   struct {
      enum { MFixed, MHint, MAny } rkind;
      Addr start;
      Addr len;
   }
   MapRequest;

extern
Bool VG_(aspacem_is_valid_for_client)( Addr start, SizeT len, UInt prot );

extern
Bool VG_(aspacem_getAdvisory)
     ( MapRequest* req, Bool forClient, /*OUT*/Addr* result );

extern
SysRes VG_(mmap_file_fixed_client)
     ( void* startV, SizeT length, Int prot, Int fd, SizeT offset );

extern
SysRes VG_(mmap_anon_fixed_client)
     ( void* startV, SizeT length, Int prot );

extern
SysRes VG_(mmap_anon_float_client)
     ( SizeT length, Int prot );

extern
SysRes VG_(map_anon_float_valgrind)( SizeT cszB );

extern ULong VG_(aspacem_get_anonsize_total)( void );

extern SysRes VG_(munmap_client)( Addr base, SizeT length );

/* This function is dangerous -- it can cause aspacem's view of the
   address space to diverge from that of the kernel.  DO NOT USE IT
   UNLESS YOU UNDERSTAND the request-notify model used by aspacem. */
extern
SysRes VG_(aspacem_do_mmap_NO_NOTIFY)( Addr start, SizeT length, UInt prot, 
                                       UInt flags, UInt fd, OffT offset);

extern 
void VG_(notify_client_mmap)( Addr a, SizeT len, UInt prot, UInt flags,
                              Int fd, SizeT offset );

extern
void VG_(notify_client_mprotect)( Addr a, SizeT len, UInt prot );

extern
void VG_(notify_client_munmap)( Addr start, SizeT len );


/* Finds the segment containing 'a'.  Only returns file/anon/resvn
   segments. */
extern NSegment* VG_(find_nsegment) ( Addr a );

/* Find the next segment along from HERE, if it is a file/anon/resvn
   segment. */
extern NSegment* VG_(next_nsegment) ( NSegment* here, Bool fwds );

/* Create a reservation from START .. START+LENGTH-1, with the given
   ShrinkMode.  When checking whether the reservation can be created,
   also ensure that at least abs(EXTRA) extra free bytes will remain
   above (> 0) or below (< 0) the reservation.

   The reservation will only be created if it, plus the extra-zone,
   falls entirely within a single free segment.  The returned Bool
   indicates whether the creation succeeded. */
extern 
Bool VG_(create_reservation) ( Addr start, SizeT length, 
                               ShrinkMode smode, SSizeT extra );


/* Let SEG be an anonymous mapping.  This fn extends the mapping by
   DELTA bytes, taking the space from a reservation section which must
   be adjacent.  If DELTA is positive, the segment is extended
   forwards in the address space, and the reservation must be the next
   one along.  If DELTA is negative, the segment is extended backwards
   in the address space and the reservation must be the previous one.
   DELTA must be page aligned and must not exceed the size of the
   reservation segment. */
extern
Bool VG_(extend_into_adjacent_reservation) ( NSegment* seg, SSizeT delta );


#endif   // __PUB_CORE_ASPACEMGR_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
