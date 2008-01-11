
/*--------------------------------------------------------------------*/
/*--- DebugInfo.                              pub_tool_debuginfo.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2007 Julian Seward
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

#ifndef __PUB_TOOL_DEBUGINFO_H
#define __PUB_TOOL_DEBUGINFO_H

/*====================================================================*/
/*=== Obtaining debug information                                  ===*/
/*====================================================================*/

/* Get the file/function/line number of the instruction at address
   'a'.  For these four, if debug info for the address is found, it
   copies the info into the buffer/UInt and returns True.  If not, it
   returns False and nothing is copied.  VG_(get_fnname) always
   demangles C++ function names.  VG_(get_fnname_w_offset) is the
   same, except it appends "+N" to symbol names to indicate offsets.  */
extern Bool VG_(get_filename) ( Addr a, Char* filename, Int n_filename );
extern Bool VG_(get_fnname)   ( Addr a, Char* fnname,   Int n_fnname   );
extern Bool VG_(get_linenum)  ( Addr a, UInt* linenum );
extern Bool VG_(get_fnname_w_offset)
                              ( Addr a, Char* fnname,   Int n_fnname   );

/* This one is the most general.  It gives filename, line number and
   optionally directory name.  filename and linenum may not be NULL.
   dirname may be NULL, meaning that the caller does not want
   directory name info, in which case dirname_available must also be
   NULL.  If dirname is non-null, directory info is written to it, if
   it is available; if not available, '\0' is written to the first
   byte.  In either case *dirname_available is set to indicate whether
   or not directory information was available.

   Returned value indicates whether any filename/line info could be
   found. */
extern Bool VG_(get_filename_linenum)
                              ( Addr a, 
                                /*OUT*/Char* filename, Int n_filename,
                                /*OUT*/Char* dirname,  Int n_dirname,
                                /*OUT*/Bool* dirname_available,
                                /*OUT*/UInt* linenum );

/* Succeeds only if we find from debug info that 'a' is the address of the
   first instruction in a function -- as opposed to VG_(get_fnname) which
   succeeds if we find from debug info that 'a' is the address of any
   instruction in a function.  Use this to instrument the start of
   a particular function.  Nb: if an executable/shared object is stripped
   of its symbols, this function will not be able to recognise function
   entry points within it. */
extern Bool VG_(get_fnname_if_entry) ( Addr a, Char* fnname, Int n_fnname );

/* Looks up 'a' in the collection of data symbols, and if found puts
   its name (or as much as will fit) into dname[0 .. n_dname-1]
   including zero terminator.  Also the 'a's offset from the symbol
   start is put into *offset. */
extern Bool VG_(get_dataname_and_offset)( Addr a,
                                          /*OUT*/Char* dname, Int n_dname,
                                          /*OUT*/OffT* offset );

/* Succeeds if the address is within a shared object or the main executable.
   It doesn't matter if debug info is present or not. */
extern Bool VG_(get_objname)  ( Addr a, Char* objname,  Int n_objname );

/* Puts into 'buf' info about the code address %eip:  the address, function
   name (if known) and filename/line number (if known), like this:

      0x4001BF05: realloc (vg_replace_malloc.c:339)

   'n_buf' gives length of 'buf'.  Returns 'buf'.
*/
extern Char* VG_(describe_IP)(Addr eip, Char* buf, Int n_buf);

/*====================================================================*/
/*=== Obtaining segment information                                ===*/
/*====================================================================*/

/* A way to get information about what segments are mapped */
typedef struct _DebugInfo DebugInfo;

/* Returns NULL if the DebugInfo isn't found.  It doesn't matter if debug info
   is present or not. */
extern       DebugInfo* VG_(find_seginfo)      ( Addr a );

/* Fish bits out of DebugInfos. */
extern       Addr     VG_(seginfo_start)     ( const DebugInfo *di );
extern       SizeT    VG_(seginfo_size)      ( const DebugInfo *di );
extern const UChar*   VG_(seginfo_soname)    ( const DebugInfo *di );
extern const UChar*   VG_(seginfo_filename)  ( const DebugInfo *di );
extern       ULong    VG_(seginfo_sym_offset)( const DebugInfo *di );

/* Function for traversing the seginfo list.  When called with NULL it
   returns the first element; otherwise it returns the given element's
   successor. */
extern const DebugInfo* VG_(next_seginfo)    ( const DebugInfo *di );

/* Functions for traversing all the symbols in a DebugInfo.  _howmany
   tells how many there are.  _getidx retrieves the n'th, for n in 0
   .. _howmany-1.  You may not modify the function name thereby
   acquired; if you want to do so, first strdup it. */
extern Int  VG_(seginfo_syms_howmany) ( const DebugInfo *di );
extern void VG_(seginfo_syms_getidx)  ( const DebugInfo *di, 
                                        Int idx,
                                        /*OUT*/Addr*   addr,
                                        /*OUT*/Addr*   tocptr,
                                        /*OUT*/UInt*   size,
                                        /*OUT*/HChar** name,
                                        /*OUT*/Bool*   isText );

/* A simple enumeration to describe the 'kind' of various kinds of
   segments that arise from the mapping of object files. */
typedef
   enum {
      Vg_SectUnknown,
      Vg_SectText,
      Vg_SectData,
      Vg_SectBSS,
      Vg_SectGOT,
      Vg_SectPLT,
      Vg_SectOPD
   }
   VgSectKind;

/* Convert a VgSectKind to a string, which must be copied if you want
   to change it. */
extern
const HChar* VG_(pp_SectKind)( VgSectKind kind );

/* Given an address 'a', make a guess of which section of which object
   it comes from.  If name is non-NULL, then the last n_name-1
   characters of the object's name is put in name[0 .. n_name-2], and
   name[n_name-1] is set to zero (guaranteed zero terminated). */
extern 
VgSectKind VG_(seginfo_sect_kind)( /*OUT*/UChar* name, SizeT n_name, 
                                   Addr a);


#endif   // __PUB_TOOL_DEBUGINFO_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
