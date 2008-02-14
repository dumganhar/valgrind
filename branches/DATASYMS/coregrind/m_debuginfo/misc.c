
/*--------------------------------------------------------------------*/
/*--- Misc simple stuff lacking a better home.              misc.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2008-2008 OpenWorks LLP
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

   Neither the names of the U.S. Department of Energy nor the
   University of California nor the names of its contributors may be
   used to endorse or promote products derived from this software
   without prior written permission.
*/

#include "pub_core_basics.h"
#include "pub_core_libcbase.h"
#include "pub_core_libcassert.h"
#include "pub_core_mallocfree.h"
#include "pub_core_xarray.h"

#include "priv_misc.h"            /* self */


void* ML_(dinfo_zalloc) ( SizeT szB ) {
   void* v;
   vg_assert(szB > 0);
   v = VG_(arena_malloc)( VG_AR_DINFO, szB );
   vg_assert(v);
   VG_(memset)(v, 0, szB);
   return v;
}

void ML_(dinfo_free) ( void* v ) {
   VG_(arena_free)( VG_AR_DINFO, v );
}

UChar* ML_(dinfo_strdup) ( const UChar* str ) {
   return VG_(arena_strdup)( VG_AR_DINFO, str );
}

UChar* ML_(dinfo_memdup)( UChar* mem, UWord nbytes ) {
   UChar* r = VG_(arena_malloc)( VG_AR_DINFO, nbytes );
   if (nbytes > 0)
      VG_(memcpy)( r, mem, nbytes );
   return r;
}


void ML_(copy_bytes_into_XA) ( XArray* /* of UChar */ xa, 
                               void* bytes, Word nbytes ) {
   Word i;
   for (i = 0; i < nbytes; i++)
      VG_(addToXA)( xa, & ((UChar*)bytes)[i] );
}


/*--------------------------------------------------------------------*/
/*--- end                                                   misc.c ---*/
/*--------------------------------------------------------------------*/
