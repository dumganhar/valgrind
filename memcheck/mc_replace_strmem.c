
/*--------------------------------------------------------------------*/
/*--- Replacements for strcpy(), memcpy() et al, which run on the  ---*/
/*--- simulated CPU.                                               ---*/
/*---                                          mc_replace_strmem.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of MemCheck, a heavyweight Valgrind skin for
   detecting memory errors.

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

#include "vg_skin.h"

#define __VALGRIND_SOMESKIN_H
#include "valgrind.h"

/* For snprintf(), ok because on simd CPU */
#include <stdio.h>

/* ---------------------------------------------------------------------
   The normal versions of these functions are hyper-optimised, which fools
   Memcheck and cause spurious value warnings.  So we replace them with
   simpler versions.  THEY RUN ON SIMD CPU!
   ------------------------------------------------------------------ */

static __inline__
Bool is_overlap ( void* dst, const void* src, UInt len )
{
   Int diff = src-dst;

   if (diff < 0) 
      diff = -diff;

   return (diff < len);
}

static __inline__
void complain2 ( Char* s, char* dst, const char* src )
{
   Char  buf[100];
   int   res = 0;    /* unused; initialise to shut gcc up */

   snprintf(buf, 100,
            "Warning: src and dst overlap in %s(%p, %p)", s, dst, src );
   VALGRIND_MAGIC_SEQUENCE(res, 0, /* irrelevant default */
                           VG_USERREQ__LOGMESSAGE, buf, 0, 0, 0);
}

static __inline__
void complain3 ( Char* s, void* dst, const void* src, int n )
{
   Char  buf[100];
   int   res = 0;    /* unused; initialise to shut gcc up */

   snprintf(buf, 100,
            "Warning: src and dst overlap in %s(%p, %p, %d)", s, dst, src, n );
   VALGRIND_MAGIC_SEQUENCE(res, 0, /* irrelevant default */
                           VG_USERREQ__LOGMESSAGE, buf, 0, 0, 0);
}

char* strrchr ( const char* s, int c )
{
   UChar  ch   = (UChar)((UInt)c);
   UChar* p    = (UChar*)s;
   UChar* last = NULL;
   while (True) {
      if (*p == ch) last = p;
      if (*p == 0) return last;
      p++;
   }
}

char* strchr ( const char* s, int c )
{
   UChar  ch = (UChar)((UInt)c);
   UChar* p  = (UChar*)s;
   while (True) {
      if (*p == ch) return p;
      if (*p == 0) return NULL;
      p++;
   }
}

char* strcat ( char* dst, const char* src )
{
   Char* dst_orig = dst;
   while (*dst) dst++;
   while (*src) *dst++ = *src++;
   *dst = 0;

   /* This is a bit redundant, I think;  any overlap and the strcat will
      go forever... or until a seg fault occurs. */
   if (is_overlap(dst, src, (Addr)dst-(Addr)dst_orig+1))
      complain2("strcat", dst, src);

   return dst_orig;
}

char* strncat ( char* dst, const char* src, int n )
{
   Char* dst_orig = dst;
   Int   m = 0;

   while (*dst) dst++;
   while (*src && m++ < n) *dst++ = *src++;  /* concat at most n chars */
   *dst = 0;                                 /* then add null (always) */

   /* This checks for overlap after copying, unavoidable without
      pre-counting lengths... should be ok */
   if (is_overlap(dst, src, (Addr)dst-(Addr)dst_orig+1))
      complain3("strncat", dst, src, n);

   return dst_orig;
}

unsigned int strlen ( const char* str )
{
   UInt i = 0;
   while (str[i] != 0) i++;
   return i;
}

char* strcpy ( char* dst, const char* src )
{
   Char* dst_orig = dst;

   while (*src) *dst++ = *src++;
   *dst = 0;

   /* This checks for overlap after copying, unavoidable without
      pre-counting length... should be ok */
   if (is_overlap(dst, src, (Addr)dst-(Addr)dst_orig+1))
      complain2("strcpy", dst, src);

   return dst_orig;
}

char* strncpy ( char* dst, const char* src, int n )
{
   Char* dst_orig = dst;
   Int   m = 0;

   if (is_overlap(dst, src, n))
      complain3("strncpy", dst, src, n);

   while (*src && m++ < n) *dst++ = *src++;
   while (m++ < n) *dst++ = 0;         /* must pad remainder with nulls */

   return dst_orig;
}

int strncmp ( const unsigned char* s1, const unsigned char* s2, 
              unsigned int nmax )
{
   unsigned int n = 0;
   while (True) {
      if (n >= nmax) return 0;
      if (*s1 == 0 && *s2 == 0) return 0;
      if (*s1 == 0) return -1;
      if (*s2 == 0) return 1;

      if (*(unsigned char*)s1 < *(unsigned char*)s2) return -1;
      if (*(unsigned char*)s1 > *(unsigned char*)s2) return 1;

      s1++; s2++; n++;
   }
}

int strcmp ( const char* s1, const char* s2 )
{
   register unsigned char c1;
   register unsigned char c2;
   while (True) {
      c1 = *(unsigned char *)s1;
      c2 = *(unsigned char *)s2;
      if (c1 != c2) break;
      if (c1 == 0) break;
      s1++; s2++;
   }
   if ((unsigned char)c1 < (unsigned char)c2) return -1;
   if ((unsigned char)c1 > (unsigned char)c2) return 1;
   return 0;
}

void* memchr(const void *s, int c, unsigned int n)
{
   unsigned int i;
   UChar c0 = (UChar)c;
   UChar* p = (UChar*)s;
   for (i = 0; i < n; i++)
      if (p[i] == c0) return (void*)(&p[i]);
   return NULL;
}

void* memcpy( void *dst, const void *src, unsigned int len )
{
   register char *d;
   register char *s;

   if (is_overlap(dst, src, len))
      complain3("memcpy", dst, src, len);
      
   if ( dst > src ) {
      d = (char *)dst + len - 1;
      s = (char *)src + len - 1;
      while ( len >= 4 ) {
         *d-- = *s--;
         *d-- = *s--;
         *d-- = *s--;
         *d-- = *s--;
         len -= 4;
      }
      while ( len-- ) {
         *d-- = *s--;
      }
   } else if ( dst < src ) {
      d = (char *)dst;
      s = (char *)src;
      while ( len >= 4 ) {
         *d++ = *s++;
         *d++ = *s++;
         *d++ = *s++;
         *d++ = *s++;
         len -= 4;
      }
      while ( len-- ) {
         *d++ = *s++;
      }
   }
   return dst;
}


/*--------------------------------------------------------------------*/
/*--- end                                      mc_replace_strmem.c ---*/
/*--------------------------------------------------------------------*/
