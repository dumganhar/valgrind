
/*--------------------------------------------------------------------*/
/*--- Intra-Valgrind interfaces for symtypes.c.    priv_symtypes.h ---*/
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

#ifndef __PRIV_SYMTYPES_H
#define __PRIV_SYMTYPES_H

/* Lets try to make these opaque */
typedef struct _SymType SymType;

/* ------------------------------------------------------------
   Constructors for various SymType nodes
   ------------------------------------------------------------ */

/* Find the basetype for a given type: that is, if type is a typedef,
   return the typedef'd type.  If resolve is true, it will resolve
   unresolved symbols.  If type is not a typedef, then this is just
   returns type.
*/
SymType *ML_(st_basetype)(SymType *type, Bool resolve);

void ML_(st_setname)(SymType *ty, Char *name);

typedef void (SymResolver)(SymType *, void *);

/* Create an unresolved type */
SymType *ML_(st_mkunresolved)(SymType *, SymResolver *resolve, void *data);

/* update an unresolved type's data */
void ML_(st_unresolved_setdata)(SymType *, SymResolver *resolve, void *data);

Bool ML_(st_isresolved)(SymType *);
UInt ML_(st_sizeof)(SymType *);

/* Unknown type (unparsable) */
SymType *ML_(st_mkunknown)(SymType *);

SymType *ML_(st_mkvoid)(SymType *);

SymType *ML_(st_mkint)(SymType *, UInt size, Bool isSigned);
SymType *ML_(st_mkbool)(SymType *, UInt size);
SymType *ML_(st_mkchar)(SymType *, Bool isSigned);
SymType *ML_(st_mkfloat)(SymType *, UInt size);
SymType *ML_(st_mkdouble)(SymType *, UInt size);

SymType *ML_(st_mkpointer)(SymType *, SymType *);
SymType *ML_(st_mkrange)(SymType *, SymType *, Int min, Int max);

SymType *ML_(st_mkstruct)(SymType *, UInt size, UInt nfields);
SymType *ML_(st_mkunion)(SymType *, UInt size, UInt nfields);
void ML_(st_addfield)(SymType *, Char *name, SymType *, UInt off, UInt size);

SymType *ML_(st_mkenum)(SymType *, UInt ntags);
SymType *ML_(st_addtag)(SymType *, Char *name, Int val);

SymType *ML_(st_mkarray)(SymType *, SymType *idxtype, SymType *artype);

SymType *ML_(st_mktypedef)(SymType *, Char *name, SymType *type);

Bool ML_(st_isstruct)(SymType *);
Bool ML_(st_isunion)(SymType *);
Bool ML_(st_isenum)(SymType *);

/* ------------------------------------------------------------
   Interface with symtab.c
   ------------------------------------------------------------ */

/* Typed value */
typedef struct _Variable Variable;

struct _Variable {
   Char		*name;		/* name */
   SymType	*type;		/* type of value */
   Addr		valuep;		/* pointer to value */
   UInt		size;		/* size of value */
   UInt		distance;	/* "distance" from site of interest */
   Variable	*next;
   Variable	*container;
};

Variable *ML_(get_scope_variables)(ThreadId tid);

#endif // __PRIV_SYMTYPES_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
