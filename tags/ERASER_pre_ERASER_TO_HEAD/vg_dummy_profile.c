
/*--------------------------------------------------------------------*/
/*--- Dummy profiling machinery -- overridden by skins when they   ---*/
/*--- want profiling.                                              ---*/
/*---                                           vg_dummy_profile.c ---*/
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

   The GNU General Public License is contained in the file COPYING.
*/

#include "vg_include.h"


void VGP_(register_profile_event) ( Int n, Char* name )
{
}

void VGP_(init_profiling) ( void )
{
   VG_(printf)(
      "\nProfiling error:\n"
      "  The --profile=yes option was specified, but the skin\n"
      "  wasn't built for profiling.  #include \"vg_profile.c\"\n"
      "  into the skin and rebuild to allow profiling.\n\n");
   VG_(exit)(1);
}

void VGP_(done_profiling) ( void )
{
   VG_(panic)("done_profiling");
}

void VGP_(pushcc) ( UInt cc )
{
   VG_(panic)("pushcc");
}

void VGP_(popcc) ( UInt cc )
{
   VG_(panic)("popcc");
}

/*--------------------------------------------------------------------*/
/*--- end                                       vg_dummy_profile.c ---*/
/*--------------------------------------------------------------------*/