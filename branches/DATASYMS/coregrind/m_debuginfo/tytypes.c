
/*--------------------------------------------------------------------*/
/*--- Representation of source level types.              tytypes.c ---*/
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
#include "pub_core_libcassert.h"
#include "pub_core_libcprint.h"
#include "pub_core_xarray.h"   /* to keep priv_tytypes.h happy */
#include "priv_misc.h"         /* dinfo_zalloc/free/strdup */
#include "priv_tytypes.h"      /* self */


TyAdmin* ML_(new_TyAdmin) ( UWord cuOff, TyAdmin* next ) {
   TyAdmin* admin = ML_(dinfo_zalloc)( sizeof(TyAdmin) );
   admin->cuOff = cuOff;
   admin->next  = next;
   return admin;
}
TyAtom* ML_(new_TyAtom) ( UChar* name, Long value ) {
   TyAtom* atom = ML_(dinfo_zalloc)( sizeof(TyAtom) );
   atom->name  = name;
   atom->value = value;
   return atom;
}
TyField* ML_(new_TyField) ( UChar* name,
                            Type* typeR, D3Expr* loc ) {
   TyField* field = ML_(dinfo_zalloc)( sizeof(TyField) );
   field->name  = name;
   field->typeR = typeR;
   field->loc   = loc;
   return field;
}
TyBounds* ML_(new_TyBounds) ( void ) {
   TyBounds* bounds = ML_(dinfo_zalloc)( sizeof(TyBounds) );
   bounds->magic = TyBounds_MAGIC;
   return bounds;
}
D3Expr* ML_(new_D3Expr) ( UChar* bytes, UWord nbytes ) {
   D3Expr* expr = ML_(dinfo_zalloc)( sizeof(D3Expr) );
   expr->bytes = bytes;
   expr->nbytes = nbytes;
   return expr;
}
Type* ML_(new_Type) ( void ) {
   Type* type = ML_(dinfo_zalloc)( sizeof(Type) );
   return type;
}

static void pp_XArray_of_pointersOrRefs ( XArray* xa ) {
   Word i;
   VG_(printf)("{");
   for (i = 0; i < VG_(sizeXA)(xa); i++) {
      void* ptr = *(void**) VG_(indexXA)(xa, i);
      VG_(printf)("0x%05lx", ptr);
      if (i+1 < VG_(sizeXA)(xa))
         VG_(printf)(",");
   }
   VG_(printf)("}");
}
void ML_(pp_TyAtom) ( TyAtom* atom ) {
   VG_(printf)("TyAtom(%lld,\"%s\")", atom->value, atom->name);
}
void ML_(pp_D3Expr) ( D3Expr* expr ) {
   VG_(printf)("D3Expr(%p,%lu)", expr->bytes, expr->nbytes);
}
void ML_(pp_TyField) ( TyField* field ) {
   VG_(printf)("TyField(0x%05lx,%p,\"%s\")",
               field->typeR, field->loc,
               field->name ? field->name : (UChar*)"");
}
void ML_(pp_TyBounds) ( TyBounds* bounds ) {
   vg_assert(bounds->magic == TyBounds_MAGIC);
   VG_(printf)("TyBounds[");
   if (bounds->knownL)
      VG_(printf)("%lld", bounds->boundL);
   else
      VG_(printf)("??");
   VG_(printf)(",");
   if (bounds->knownU)
      VG_(printf)("%lld", bounds->boundU);
   else
      VG_(printf)("??");
   VG_(printf)("]");
}

static void pp_TyBounds_C_ishly ( TyBounds* bounds ) {
   vg_assert(bounds->magic == TyBounds_MAGIC);
   if (bounds->knownL && bounds->knownU && bounds->boundL == 0) {
      VG_(printf)("[%lld]", 1 + bounds->boundU);
   }
   else
   if (bounds->knownL && (!bounds->knownU) && bounds->boundL == 0) {
      VG_(printf)("[]");
   }
   else
      ML_(pp_TyBounds)( bounds );
}


void ML_(pp_Type) ( Type* ty )
{
   switch (ty->tag) {
      case Ty_Base:
         VG_(printf)("Ty_Base(%d,%c,\"%s\")",
                     ty->Ty.Base.szB, ty->Ty.Base.enc,
                     ty->Ty.Base.name ? ty->Ty.Base.name
                                        : (UChar*)"(null)" );
         break;
      case Ty_PorR:
         VG_(printf)("Ty_PorR(%d,%c,0x%05lx)",
                     ty->Ty.PorR.szB, 
                     ty->Ty.PorR.isPtr ? 'P' : 'R',
                     ty->Ty.PorR.typeR);
         break;
      case Ty_Enum:
         VG_(printf)("Ty_Enum(%d,%p,\"%s\")",
                     ty->Ty.Enum.szB, ty->Ty.Enum.atomRs,
                     ty->Ty.Enum.name ? ty->Ty.Enum.name
                                        : (UChar*)"" );
         if (ty->Ty.Enum.atomRs)
            pp_XArray_of_pointersOrRefs( ty->Ty.Enum.atomRs );
         break;
      case Ty_StOrUn:
         if (ty->Ty.StOrUn.complete) {
            VG_(printf)("Ty_StOrUn(%d,%c,%p,\"%s\")",
                        ty->Ty.StOrUn.szB, 
                        ty->Ty.StOrUn.isStruct ? 'S' : 'U',
                        ty->Ty.StOrUn.fields,
                        ty->Ty.StOrUn.name ? ty->Ty.StOrUn.name
                                             : (UChar*)"" );
            if (ty->Ty.StOrUn.fields)
               pp_XArray_of_pointersOrRefs( ty->Ty.StOrUn.fields );
         } else {
            VG_(printf)("Ty_StOrUn(INCOMPLETE,\"%s\")",
                        ty->Ty.StOrUn.name);
         }
         break;
      case Ty_Array:
         VG_(printf)("Ty_Array(0x%05lx,%p)",
                     ty->Ty.Array.typeR, ty->Ty.Array.bounds);
         if (ty->Ty.Array.bounds)
            pp_XArray_of_pointersOrRefs( ty->Ty.Array.bounds );
         break;
      case Ty_TyDef:
         VG_(printf)("Ty_TyDef(0x%05lx,\"%s\")",
                     ty->Ty.TyDef.typeR,
                     ty->Ty.TyDef.name ? ty->Ty.TyDef.name
                                         : (UChar*)"" );
         break;
      case Ty_Fn:
         VG_(printf)("Ty_Fn");
         break;
      case Ty_Qual:
         VG_(printf)("Ty_Qual(%c,0x%05lx)", ty->Ty.Qual.qual,
                     ty->Ty.Qual.typeR);
         break;
      case Ty_Void:
         VG_(printf)("Ty_Void%s",
                     ty->Ty.Void.isFake ? "(fake)" : "");
         break;
      default: VG_(printf)("pp_Type:???");
         break;
   }
}
void ML_(pp_TyAdmin) ( TyAdmin* admin ) {
   if (admin->cuOff != -1UL) {
      VG_(printf)("<%05lx,%p> ", admin->cuOff, admin->payload);
   } else {
      VG_(printf)("<ff..f,%p> ", admin->payload);
   }
   switch (admin->tag) {
      case TyA_Type:   ML_(pp_Type)(admin->payload);     break;
      case TyA_Atom:   ML_(pp_TyAtom)(admin->payload);   break;
      case TyA_Expr:   ML_(pp_D3Expr)(admin->payload);   break;
      case TyA_Field:  ML_(pp_TyField)(admin->payload);  break;
      case TyA_Bounds: ML_(pp_TyBounds)(admin->payload); break;
      default:         VG_(printf)("pp_TyAdmin:???");    break;
   }
}

/* NOTE: this assumes that the types have all been 'resolved' (that
   is, inter-type references expressed as .debug_info offsets have
   been converted into pointers) */
void ML_(pp_Type_C_ishly) ( void* /* Type* */ tyV )
{
   Type* ty = (Type*)tyV;

   switch (ty->tag) {
      case Ty_Base:
         if (!ty->Ty.Base.name) goto unhandled;
         VG_(printf)("%s", ty->Ty.Base.name);
         break;
      case Ty_PorR:
         ML_(pp_Type_C_ishly)(ty->Ty.PorR.typeR);
         VG_(printf)("%s", ty->Ty.PorR.isPtr ? "*" : "&");
         break;
      case Ty_Enum:
         if (!ty->Ty.Enum.name) goto unhandled;
         VG_(printf)("enum %s", ty->Ty.Enum.name);
         break;
      case Ty_StOrUn:
         if (!ty->Ty.StOrUn.name) goto unhandled;
         VG_(printf)("%s %s",
                     ty->Ty.StOrUn.isStruct ? "struct" : "union",
                     ty->Ty.StOrUn.name);
         break;
      case Ty_Array:
         ML_(pp_Type_C_ishly)(ty->Ty.Array.typeR);
         if (ty->Ty.Array.bounds) {
            Word    w;
            XArray* xa = ty->Ty.Array.bounds;
            for (w = 0; w < VG_(sizeXA)(xa); w++) {
               pp_TyBounds_C_ishly( *(TyBounds**)VG_(indexXA)(xa, w) );
            }
         } else {
            VG_(printf)("%s", "[??]");
         }
         break;
      case Ty_TyDef:
         if (!ty->Ty.TyDef.name) goto unhandled;
         VG_(printf)("%s", ty->Ty.TyDef.name);
         break;
      case Ty_Fn:
         VG_(printf)("%s", "<function_type>");
         break;
      case Ty_Qual:
         switch (ty->Ty.Qual.qual) {
            case 'C': VG_(printf)("const "); break;
            case 'V': VG_(printf)("volatile "); break;
            default: goto unhandled;
         }
         ML_(pp_Type_C_ishly)(ty->Ty.Qual.typeR);
         break;
      case Ty_Void:
         VG_(printf)("%svoid",
                     ty->Ty.Void.isFake ? "fake" : "");
         break;
      default: VG_(printf)("pp_Type_C_ishly:???");
         break;
   }
   return;

  unhandled:
   ML_(pp_Type)(ty);
}


/* How big is this type?  (post-resolved only) */
/* FIXME: check all pointers before dereferencing */
SizeT ML_(sizeOfType)( void* /* Type */ tyV )
{
   SizeT   eszB;
   Word    i;
   Type* ty = (Type*)tyV;
   switch (ty->tag) {
      case Ty_Base:
         return ty->Ty.Base.szB;
      case Ty_Qual:
         return ML_(sizeOfType)( ty->Ty.Qual.typeR );
      case Ty_TyDef:
         if (!ty->Ty.TyDef.typeR)
            return 0; /*UNKNOWN*/
         return ML_(sizeOfType)( ty->Ty.TyDef.typeR );
      case Ty_PorR:
         vg_assert(ty->Ty.PorR.szB == 4 || ty->Ty.PorR.szB == 8);
         return ty->Ty.PorR.szB;
      case Ty_StOrUn:
         return ty->Ty.StOrUn.szB;
      case Ty_Enum:
         return ty->Ty.Enum.szB;
      case Ty_Array:
         if (!ty->Ty.Array.typeR)
            return 0;
         eszB = ML_(sizeOfType)( ty->Ty.Array.typeR );
         for (i = 0; i < VG_(sizeXA)( ty->Ty.Array.bounds ); i++) {
            TyBounds* bo
               = *(TyBounds**)VG_(indexXA)(ty->Ty.Array.bounds, i);
            vg_assert(bo);
            if (!(bo->knownL && bo->knownU))
               return 0;
            eszB *= (SizeT)( bo->boundU - bo->boundL + 1 );
         }
         return eszB;
      default:
         VG_(printf)("ML_(sizeOfType): unhandled: ");
         ML_(pp_Type)(tyV);
         VG_(printf)("\n");
         vg_assert(0);
   }
}

/*--------------------------------------------------------------------*/
/*--- end                                                tytypes.c ---*/
/*--------------------------------------------------------------------*/
