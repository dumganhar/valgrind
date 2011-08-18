
/*--------------------------------------------------------------------*/
/*--- Replacements for strcpy(), memcpy() et al, which run on the  ---*/
/*--- simulated CPU.                                               ---*/
/*---                                          mc_replace_strmem.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of MemCheck, a heavyweight Valgrind tool for
   detecting memory errors.

   Copyright (C) 2000-2010 Julian Seward 
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

#include "pub_tool_basics.h"
#include "pub_tool_hashtable.h"
#include "pub_tool_redir.h"
#include "pub_tool_tooliface.h"
#include "valgrind.h"

#include "mc_include.h"
#include "memcheck.h"

/* ---------------------------------------------------------------------
   We have our own versions of these functions for two reasons:
   (a) it allows us to do overlap checking
   (b) some of the normal versions are hyper-optimised, which fools
       Memcheck and cause spurious value warnings.  Our versions are
       simpler.

   Note that overenthusiastic use of PLT bypassing by the glibc people also
   means that we need to patch multiple versions of some of the functions to
   our own implementations.

   THEY RUN ON THE SIMD CPU!
   ------------------------------------------------------------------ */

/* Assignment of behavioural equivalence class tags: 2NNN is intended
   to be reserved for Memcheck.  Current usage:

   2001 STRRCHR
   2002 STRCHR
   2003 STRCAT
   2004 STRNCAT
   2005 STRLCAT
   2006 STRNLEN
   2007 STRLEN
   2008 STRCPY
   2009 STRNCPY
   2010 STRLCPY
   2011 STRNCMP
   2012 STRCASECMP
   2013 STRNCASECMP
   2014 STRCASECMP_L
   2015 STRNCASECMP_L
   2016 STRCMP
   2017 MEMCHR
   2018 MEMMOVE
   2019 MEMCMP
   2020 STPCPY
   2021 MEMSET
   2022 MEMCPY
   2023 BCOPY
   2024 GLIBC25___MEMMOVE_CHK
   2025 GLIBC232_STRCHRNUL
   2026 GLIBC232_RAWMEMCHR
   2027 GLIBC25___STRCPY_CHK
   2028 GLIBC25___STPCPY_CHK
   2029 GLIBC25_MEMPCPY
   2030 GLIBC26___MEMCPY_CHK
   2031 STRSTR
   2032 STRPBRK
   2033 STRCSPN
   2034 STRSPN
*/


/* Figure out if [dst .. dst+dstlen-1] overlaps with 
                 [src .. src+srclen-1].
   We assume that the address ranges do not wrap around
   (which is safe since on Linux addresses >= 0xC0000000
   are not accessible and the program will segfault in this
   circumstance, presumably).
*/
static inline
Bool is_overlap ( void* dst, const void* src, SizeT dstlen, SizeT srclen )
{
   Addr loS, hiS, loD, hiD;

   if (dstlen == 0 || srclen == 0)
      return False;

   loS = (Addr)src;
   loD = (Addr)dst;
   hiS = loS + srclen - 1;
   hiD = loD + dstlen - 1;

   /* So figure out if [loS .. hiS] overlaps with [loD .. hiD]. */
   if (loS < loD) {
      return !(hiS < loD);
   }
   else if (loD < loS) {
      return !(hiD < loS);
   }
   else { 
      /* They start at same place.  Since we know neither of them has
         zero length, they must overlap. */
      return True;
   }
}


/* Call here to exit if we can't continue.  On Android we can't call
   _exit for some reason, so we have to blunt-instrument it. */
__attribute__ ((__noreturn__))
static inline void my_exit ( int x )
{
#  if defined(VGPV_arm_linux_android)
   __asm__ __volatile__(".word 0xFFFFFFFF");
   while (1) {}
#  else
   extern void _exit(int status);
   _exit(x);
#  endif
}


// This is a macro rather than a function because we don't want to have an
// extra function in the stack trace.
#define RECORD_OVERLAP_ERROR(s, src, dst, len)                  \
  VALGRIND_DO_CLIENT_REQUEST_EXPR(0,                            \
                  _VG_USERREQ__MEMCHECK_RECORD_OVERLAP_ERROR,   \
                  s, src, dst, len, 0)


#define STRRCHR(soname, fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2001,soname,fnname)( const char* s, int c ); \
   char* VG_REPLACE_FUNCTION_EZU(2001,soname,fnname)( const char* s, int c ) \
   { \
      UChar  ch   = (UChar)((UInt)c); \
      UChar* p    = (UChar*)s; \
      UChar* last = NULL; \
      while (True) { \
         if (*p == ch) last = p; \
         if (*p == 0) return last; \
         p++; \
      } \
   }

// Apparently rindex() is the same thing as strrchr()
STRRCHR(VG_Z_LIBC_SONAME,   strrchr)
STRRCHR(VG_Z_LIBC_SONAME,   rindex)
#if defined(VGO_linux)
STRRCHR(VG_Z_LIBC_SONAME,   __GI_strrchr)
STRRCHR(VG_Z_LD_LINUX_SO_2, rindex)
#elif defined(VGO_darwin)
STRRCHR(VG_Z_DYLD,          strrchr)
STRRCHR(VG_Z_DYLD,          rindex)
#endif
   

#define STRCHR(soname, fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2002,soname,fnname) ( const char* s, int c ); \
   char* VG_REPLACE_FUNCTION_EZU(2002,soname,fnname) ( const char* s, int c ) \
   { \
      UChar  ch = (UChar)((UInt)c); \
      UChar* p  = (UChar*)s; \
      while (True) { \
         if (*p == ch) return p; \
         if (*p == 0) return NULL; \
         p++; \
      } \
   }

// Apparently index() is the same thing as strchr()
STRCHR(VG_Z_LIBC_SONAME,          strchr)
STRCHR(VG_Z_LIBC_SONAME,          index)
#if defined(VGO_linux)
STRCHR(VG_Z_LIBC_SONAME,          __GI_strchr)
STRCHR(VG_Z_LD_LINUX_SO_2,        strchr)
STRCHR(VG_Z_LD_LINUX_SO_2,        index)
STRCHR(VG_Z_LD_LINUX_X86_64_SO_2, strchr)
STRCHR(VG_Z_LD_LINUX_X86_64_SO_2, index)
#elif defined(VGO_darwin)
STRCHR(VG_Z_DYLD,                 strchr)
STRCHR(VG_Z_DYLD,                 index)
#endif


#define STRCAT(soname, fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2003,soname,fnname) \
            ( char* dst, const char* src ); \
   char* VG_REPLACE_FUNCTION_EZU(2003,soname,fnname) \
            ( char* dst, const char* src ) \
   { \
      const Char* src_orig = src; \
            Char* dst_orig = dst; \
      while (*dst) dst++; \
      while (*src) *dst++ = *src++; \
      *dst = 0; \
      \
      /* This is a bit redundant, I think;  any overlap and the strcat will */ \
      /* go forever... or until a seg fault occurs. */ \
      if (is_overlap(dst_orig,  \
                     src_orig,  \
                     (Addr)dst-(Addr)dst_orig+1,  \
                     (Addr)src-(Addr)src_orig+1)) \
         RECORD_OVERLAP_ERROR("strcat", dst_orig, src_orig, 0); \
      \
      return dst_orig; \
   }

STRCAT(VG_Z_LIBC_SONAME, strcat)
#if defined(VGO_linux)
STRCAT(VG_Z_LIBC_SONAME, __GI_strcat)
#endif

#define STRNCAT(soname, fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2004,soname,fnname) \
            ( char* dst, const char* src, SizeT n ); \
   char* VG_REPLACE_FUNCTION_EZU(2004,soname,fnname) \
            ( char* dst, const char* src, SizeT n ) \
   { \
      const Char* src_orig = src; \
            Char* dst_orig = dst; \
      SizeT m = 0; \
      \
      while (*dst) dst++; \
      while (m < n && *src) { m++; *dst++ = *src++; } /* concat <= n chars */ \
      *dst = 0;                                       /* always add null   */ \
      \
      /* This checks for overlap after copying, unavoidable without */ \
      /* pre-counting lengths... should be ok */ \
      if (is_overlap(dst_orig,  \
                     src_orig,  \
                     (Addr)dst-(Addr)dst_orig+1, \
                     (Addr)src-(Addr)src_orig+1)) \
         RECORD_OVERLAP_ERROR("strncat", dst_orig, src_orig, n); \
      \
      return dst_orig; \
   }

STRNCAT(VG_Z_LIBC_SONAME, strncat)
#if defined(VGO_darwin)
STRNCAT(VG_Z_DYLD,        strncat)
#endif


/* Append src to dst. n is the size of dst's buffer. dst is guaranteed 
   to be nul-terminated after the copy, unless n <= strlen(dst_orig). 
   Returns min(n, strlen(dst_orig)) + strlen(src_orig). 
   Truncation occurred if retval >= n. 
*/
#define STRLCAT(soname, fnname) \
   SizeT VG_REPLACE_FUNCTION_EZU(2005,soname,fnname) \
        ( char* dst, const char* src, SizeT n ); \
   SizeT VG_REPLACE_FUNCTION_EZU(2005,soname,fnname) \
        ( char* dst, const char* src, SizeT n ) \
   { \
      const Char* src_orig = src; \
      Char* dst_orig = dst; \
      SizeT m = 0; \
      \
      while (m < n && *dst) { m++; dst++; } \
      if (m < n) { \
         /* Fill as far as dst_orig[n-2], then nul-terminate. */ \
         while (m < n-1 && *src) { m++; *dst++ = *src++; } \
         *dst = 0; \
      } else { \
         /* No space to copy anything to dst. m == n */ \
      } \
      /* Finish counting min(n, strlen(dst_orig)) + strlen(src_orig) */ \
      while (*src) { m++; src++; } \
      /* This checks for overlap after copying, unavoidable without */ \
      /* pre-counting lengths... should be ok */ \
      if (is_overlap(dst_orig,  \
                     src_orig,  \
                     (Addr)dst-(Addr)dst_orig+1,  \
                     (Addr)src-(Addr)src_orig+1)) \
         RECORD_OVERLAP_ERROR("strlcat", dst_orig, src_orig, n); \
      \
      return m; \
   }

#if defined(VGO_darwin)
STRLCAT(VG_Z_LIBC_SONAME, strlcat)
STRLCAT(VG_Z_DYLD,        strlcat)
#endif


#define STRNLEN(soname, fnname) \
   SizeT VG_REPLACE_FUNCTION_EZU(2006,soname,fnname) \
            ( const char* str, SizeT n ); \
   SizeT VG_REPLACE_FUNCTION_EZU(2006,soname,fnname) \
            ( const char* str, SizeT n ) \
   { \
      SizeT i = 0; \
      while (i < n && str[i] != 0) i++; \
      return i; \
   }

STRNLEN(VG_Z_LIBC_SONAME, strnlen)
#if defined(VGO_linux)
STRNLEN(VG_Z_LIBC_SONAME, __GI_strnlen)
#endif
   

// Note that this replacement often doesn't get used because gcc inlines
// calls to strlen() with its own built-in version.  This can be very
// confusing if you aren't expecting it.  Other small functions in this file
// may also be inline by gcc.
#define STRLEN(soname, fnname) \
   SizeT VG_REPLACE_FUNCTION_EZU(2007,soname,fnname) \
      ( const char* str ); \
   SizeT VG_REPLACE_FUNCTION_EZU(2007,soname,fnname) \
      ( const char* str )  \
   { \
      SizeT i = 0; \
      while (str[i] != 0) i++; \
      return i; \
   }

STRLEN(VG_Z_LIBC_SONAME,          strlen)
#if defined(VGO_linux)
STRLEN(VG_Z_LIBC_SONAME,          __GI_strlen)
#endif


#define STRCPY(soname, fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2008,soname,fnname) \
      ( char* dst, const char* src ); \
   char* VG_REPLACE_FUNCTION_EZU(2008,soname,fnname) \
      ( char* dst, const char* src ) \
   { \
      const Char* src_orig = src; \
            Char* dst_orig = dst; \
      \
      while (*src) *dst++ = *src++; \
      *dst = 0; \
      \
      /* This checks for overlap after copying, unavoidable without */ \
      /* pre-counting length... should be ok */ \
      if (is_overlap(dst_orig,  \
                     src_orig,  \
                     (Addr)dst-(Addr)dst_orig+1, \
                     (Addr)src-(Addr)src_orig+1)) \
         RECORD_OVERLAP_ERROR("strcpy", dst_orig, src_orig, 0); \
      \
      return dst_orig; \
   }

STRCPY(VG_Z_LIBC_SONAME, strcpy)
#if defined(VGO_linux)
STRCPY(VG_Z_LIBC_SONAME, __GI_strcpy)
#elif defined(VGO_darwin)
STRCPY(VG_Z_DYLD,        strcpy)
#endif


#define STRNCPY(soname, fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2009,soname,fnname) \
            ( char* dst, const char* src, SizeT n ); \
   char* VG_REPLACE_FUNCTION_EZU(2009,soname,fnname) \
            ( char* dst, const char* src, SizeT n ) \
   { \
      const Char* src_orig = src; \
            Char* dst_orig = dst; \
      SizeT m = 0; \
      \
      while (m   < n && *src) { m++; *dst++ = *src++; } \
      /* Check for overlap after copying; all n bytes of dst are relevant, */ \
      /* but only m+1 bytes of src if terminator was found */ \
      if (is_overlap(dst_orig, src_orig, n, (m < n) ? m+1 : n)) \
         RECORD_OVERLAP_ERROR("strncpy", dst, src, n); \
      while (m++ < n) *dst++ = 0;         /* must pad remainder with nulls */ \
 \
      return dst_orig; \
   }

STRNCPY(VG_Z_LIBC_SONAME, strncpy)
#if defined(VGO_linux)
STRNCPY(VG_Z_LIBC_SONAME, __GI_strncpy)
#elif defined(VGO_darwin)
STRNCPY(VG_Z_DYLD,        strncpy)
#endif


/* Copy up to n-1 bytes from src to dst. Then nul-terminate dst if n > 0. 
   Returns strlen(src). Does not zero-fill the remainder of dst. */
#define STRLCPY(soname, fnname) \
   SizeT VG_REPLACE_FUNCTION_EZU(2010,soname,fnname) \
       ( char* dst, const char* src, SizeT n ); \
   SizeT VG_REPLACE_FUNCTION_EZU(2010,soname,fnname) \
       ( char* dst, const char* src, SizeT n ) \
   { \
      const char* src_orig = src; \
      char* dst_orig = dst; \
      SizeT m = 0; \
      \
      while (m < n-1 && *src) { m++; *dst++ = *src++; } \
      /* m non-nul bytes have now been copied, and m <= n-1. */ \
      /* Check for overlap after copying; all n bytes of dst are relevant, */ \
      /* but only m+1 bytes of src if terminator was found */ \
      if (is_overlap(dst_orig, src_orig, n, (m < n) ? m+1 : n)) \
          RECORD_OVERLAP_ERROR("strlcpy", dst, src, n); \
      /* Nul-terminate dst. */ \
      if (n > 0) *dst = 0; \
      /* Finish counting strlen(src). */ \
      while (*src) src++; \
      return src - src_orig; \
   }

#if defined(VGO_darwin)
STRLCPY(VG_Z_LIBC_SONAME, strlcpy)
STRLCPY(VG_Z_DYLD,        strlcpy)
#endif


#define STRNCMP(soname, fnname) \
   int VG_REPLACE_FUNCTION_EZU(2011,soname,fnname) \
          ( const char* s1, const char* s2, SizeT nmax ); \
   int VG_REPLACE_FUNCTION_EZU(2011,soname,fnname) \
          ( const char* s1, const char* s2, SizeT nmax ) \
   { \
      SizeT n = 0; \
      while (True) { \
         if (n >= nmax) return 0; \
         if (*s1 == 0 && *s2 == 0) return 0; \
         if (*s1 == 0) return -1; \
         if (*s2 == 0) return 1; \
         \
         if (*(unsigned char*)s1 < *(unsigned char*)s2) return -1; \
         if (*(unsigned char*)s1 > *(unsigned char*)s2) return 1; \
         \
         s1++; s2++; n++; \
      } \
   }

STRNCMP(VG_Z_LIBC_SONAME, strncmp)
#if defined(VGO_linux)
STRNCMP(VG_Z_LIBC_SONAME, __GI_strncmp)
#elif defined(VGO_darwin)
STRNCMP(VG_Z_DYLD,        strncmp)
#endif


#define STRCASECMP(soname, fnname) \
   int VG_REPLACE_FUNCTION_EZU(2012,soname,fnname) \
          ( const char* s1, const char* s2 ); \
   int VG_REPLACE_FUNCTION_EZU(2012,soname,fnname) \
          ( const char* s1, const char* s2 ) \
   { \
      extern int tolower(int); \
      register unsigned char c1; \
      register unsigned char c2; \
      while (True) { \
         c1 = tolower(*(unsigned char *)s1); \
         c2 = tolower(*(unsigned char *)s2); \
         if (c1 != c2) break; \
         if (c1 == 0) break; \
         s1++; s2++; \
      } \
      if ((unsigned char)c1 < (unsigned char)c2) return -1; \
      if ((unsigned char)c1 > (unsigned char)c2) return 1; \
      return 0; \
   }

#if !defined(VGPV_arm_linux_android)
STRCASECMP(VG_Z_LIBC_SONAME, strcasecmp)
#endif
#if defined(VGO_linux) && !defined(VGPV_arm_linux_android)
STRCASECMP(VG_Z_LIBC_SONAME, __GI_strcasecmp)
#endif


#define STRNCASECMP(soname, fnname) \
   int VG_REPLACE_FUNCTION_EZU(2013,soname,fnname) \
          ( const char* s1, const char* s2, SizeT nmax ); \
   int VG_REPLACE_FUNCTION_EZU(2013,soname,fnname) \
          ( const char* s1, const char* s2, SizeT nmax ) \
   { \
      extern int tolower(int); \
      SizeT n = 0; \
      while (True) { \
         if (n >= nmax) return 0; \
         if (*s1 == 0 && *s2 == 0) return 0; \
         if (*s1 == 0) return -1; \
         if (*s2 == 0) return 1; \
         \
         if (tolower(*(unsigned char*)s1) \
             < tolower(*(unsigned char*)s2)) return -1; \
         if (tolower(*(unsigned char*)s1) \
             > tolower(*(unsigned char*)s2)) return 1; \
         \
         s1++; s2++; n++; \
      } \
   }

#if !defined(VGPV_arm_linux_android)
STRNCASECMP(VG_Z_LIBC_SONAME, strncasecmp)
#endif
#if defined(VGO_linux) && !defined(VGPV_arm_linux_android)
STRNCASECMP(VG_Z_LIBC_SONAME, __GI_strncasecmp)
#elif defined(VGO_darwin)
STRNCASECMP(VG_Z_DYLD,        strncasecmp)
#endif


#define STRCASECMP_L(soname, fnname) \
   int VG_REPLACE_FUNCTION_EZU(2014,soname,fnname) \
          ( const char* s1, const char* s2, void* locale ); \
   int VG_REPLACE_FUNCTION_EZU(2014,soname,fnname) \
          ( const char* s1, const char* s2, void* locale ) \
   { \
      extern int tolower_l(int, void*) __attribute__((weak));    \
      register unsigned char c1; \
      register unsigned char c2; \
      while (True) { \
         c1 = tolower_l(*(unsigned char *)s1, locale); \
         c2 = tolower_l(*(unsigned char *)s2, locale); \
         if (c1 != c2) break; \
         if (c1 == 0) break; \
         s1++; s2++; \
      } \
      if ((unsigned char)c1 < (unsigned char)c2) return -1; \
      if ((unsigned char)c1 > (unsigned char)c2) return 1; \
      return 0; \
   }

STRCASECMP_L(VG_Z_LIBC_SONAME, strcasecmp_l)
#if defined(VGO_linux)
STRCASECMP_L(VG_Z_LIBC_SONAME, __GI_strcasecmp_l)
STRCASECMP_L(VG_Z_LIBC_SONAME, __GI___strcasecmp_l)
#endif


#define STRNCASECMP_L(soname, fnname) \
   int VG_REPLACE_FUNCTION_EZU(2015,soname,fnname) \
          ( const char* s1, const char* s2, SizeT nmax, void* locale ); \
   int VG_REPLACE_FUNCTION_EZU(2015,soname,fnname) \
          ( const char* s1, const char* s2, SizeT nmax, void* locale ) \
   { \
      extern int tolower_l(int, void*) __attribute__((weak));    \
      SizeT n = 0; \
      while (True) { \
         if (n >= nmax) return 0; \
         if (*s1 == 0 && *s2 == 0) return 0; \
         if (*s1 == 0) return -1; \
         if (*s2 == 0) return 1; \
         \
         if (tolower_l(*(unsigned char*)s1, locale) \
             < tolower_l(*(unsigned char*)s2, locale)) return -1; \
         if (tolower_l(*(unsigned char*)s1, locale) \
             > tolower_l(*(unsigned char*)s2, locale)) return 1; \
         \
         s1++; s2++; n++; \
      } \
   }

STRNCASECMP_L(VG_Z_LIBC_SONAME, strncasecmp_l)
#if defined(VGO_linux)
STRNCASECMP_L(VG_Z_LIBC_SONAME, __GI_strncasecmp_l)
#elif defined(VGO_darwin)
STRNCASECMP_L(VG_Z_DYLD,        strncasecmp_l)
#endif


#define STRCMP(soname, fnname) \
   int VG_REPLACE_FUNCTION_EZU(2016,soname,fnname) \
          ( const char* s1, const char* s2 ); \
   int VG_REPLACE_FUNCTION_EZU(2016,soname,fnname) \
          ( const char* s1, const char* s2 ) \
   { \
      register unsigned char c1; \
      register unsigned char c2; \
      while (True) { \
         c1 = *(unsigned char *)s1; \
         c2 = *(unsigned char *)s2; \
         if (c1 != c2) break; \
         if (c1 == 0) break; \
         s1++; s2++; \
      } \
      if ((unsigned char)c1 < (unsigned char)c2) return -1; \
      if ((unsigned char)c1 > (unsigned char)c2) return 1; \
      return 0; \
   }

STRCMP(VG_Z_LIBC_SONAME,          strcmp)
#if defined(VGO_linux)
STRCMP(VG_Z_LIBC_SONAME,          __GI_strcmp)
STRCMP(VG_Z_LD_LINUX_X86_64_SO_2, strcmp)
STRCMP(VG_Z_LD64_SO_1,            strcmp)
#endif


#define MEMCHR(soname, fnname) \
   void* VG_REPLACE_FUNCTION_EZU(2017,soname,fnname) \
            (const void *s, int c, SizeT n); \
   void* VG_REPLACE_FUNCTION_EZU(2017,soname,fnname) \
            (const void *s, int c, SizeT n) \
   { \
      SizeT i; \
      UChar c0 = (UChar)c; \
      UChar* p = (UChar*)s; \
      for (i = 0; i < n; i++) \
         if (p[i] == c0) return (void*)(&p[i]); \
      return NULL; \
   }

MEMCHR(VG_Z_LIBC_SONAME, memchr)
#if defined(VGO_darwin)
MEMCHR(VG_Z_DYLD,        memchr)
#endif


#define MEMMOVE_OR_MEMCPY(becTag, soname, fnname, do_ol_check)  \
   void* VG_REPLACE_FUNCTION_EZZ(becTag,soname,fnname) \
            ( void *dst, const void *src, SizeT len ); \
   void* VG_REPLACE_FUNCTION_EZZ(becTag,soname,fnname) \
            ( void *dst, const void *src, SizeT len ) \
   { \
      if (do_ol_check && is_overlap(dst, src, len, len)) \
         RECORD_OVERLAP_ERROR("memcpy", dst, src, len); \
      \
      const Addr WS = sizeof(UWord); /* 8 or 4 */ \
      const Addr WM = WS - 1;        /* 7 or 3 */ \
      \
      if (len > 0) { \
         if (dst < src) { \
         \
            /* Copying backwards. */ \
            SizeT n = len; \
            Addr  d = (Addr)dst; \
            Addr  s = (Addr)src; \
            \
            if (((s^d) & WM) == 0) { \
               /* s and d have same UWord alignment. */ \
               /* Pull up to a UWord boundary. */ \
               while ((s & WM) != 0 && n >= 1) \
                  { *(UChar*)d = *(UChar*)s; s += 1; d += 1; n -= 1; } \
               /* Copy UWords. */ \
               while (n >= WS) \
                  { *(UWord*)d = *(UWord*)s; s += WS; d += WS; n -= WS; } \
               if (n == 0) \
                  return dst; \
            } \
            if (((s|d) & 1) == 0) { \
               /* Both are 16-aligned; copy what we can thusly. */ \
               while (n >= 2) \
                  { *(UShort*)d = *(UShort*)s; s += 2; d += 2; n -= 2; } \
            } \
            /* Copy leftovers, or everything if misaligned. */ \
            while (n >= 1) \
               { *(UChar*)d = *(UChar*)s; s += 1; d += 1; n -= 1; } \
         \
         } else if (dst > src) { \
         \
            SizeT n = len; \
            Addr  d = ((Addr)dst) + n; \
            Addr  s = ((Addr)src) + n; \
            \
            /* Copying forwards. */ \
            if (((s^d) & WM) == 0) { \
               /* s and d have same UWord alignment. */ \
               /* Back down to a UWord boundary. */ \
               while ((s & WM) != 0 && n >= 1) \
                  { s -= 1; d -= 1; *(UChar*)d = *(UChar*)s; n -= 1; } \
               /* Copy UWords. */ \
               while (n >= WS) \
                  { s -= WS; d -= WS; *(UWord*)d = *(UWord*)s; n -= WS; } \
               if (n == 0) \
                  return dst; \
            } \
            if (((s|d) & 1) == 0) { \
               /* Both are 16-aligned; copy what we can thusly. */ \
               while (n >= 2) \
                  { s -= 2; d -= 2; *(UShort*)d = *(UShort*)s; n -= 2; } \
            } \
            /* Copy leftovers, or everything if misaligned. */ \
            while (n >= 1) \
               { s -= 1; d -= 1; *(UChar*)d = *(UChar*)s; n -= 1; } \
            \
         } \
      } \
      \
      return dst; \
   }

#define MEMMOVE(soname, fnname)  \
   MEMMOVE_OR_MEMCPY(2018, soname, fnname, 0)

#define MEMCPY(soname, fnname) \
   MEMMOVE_OR_MEMCPY(2022, soname, fnname, 1)

#if defined(VGO_linux)
/* For older memcpy we have to use memmove-like semantics and skip the
   overlap check; sigh; see #275284. */
MEMMOVE(VG_Z_LIBC_SONAME, memcpyZAGLIBCZu2Zd2Zd5) /* memcpy@GLIBC_2.2.5 */
MEMCPY(VG_Z_LIBC_SONAME,  memcpyZAZAGLIBCZu2Zd14) /* memcpy@@GLIBC_2.14 */
MEMCPY(VG_Z_LD_SO_1,      memcpy) /* ld.so.1 */
MEMCPY(VG_Z_LD64_SO_1,    memcpy) /* ld64.so.1 */
#elif defined(VGO_darwin)
MEMCPY(VG_Z_LIBC_SONAME,  memcpy)
MEMCPY(VG_Z_DYLD,         memcpy)
#endif
/* icc9 blats these around all over the place.  Not only in the main
   executable but various .so's.  They are highly tuned and read
   memory beyond the source boundary (although work correctly and
   never go across page boundaries), so give errors when run natively,
   at least for misaligned source arg.  Just intercepting in the exe
   only until we understand more about the problem.  See
   http://bugs.kde.org/show_bug.cgi?id=139776
 */
MEMCPY(NONE, ZuintelZufastZumemcpy)


#define MEMCMP(soname, fnname) \
   int VG_REPLACE_FUNCTION_EZU(2019,soname,fnname)       \
          ( const void *s1V, const void *s2V, SizeT n ); \
   int VG_REPLACE_FUNCTION_EZU(2019,soname,fnname)       \
          ( const void *s1V, const void *s2V, SizeT n )  \
   { \
      int res; \
      unsigned char a0; \
      unsigned char b0; \
      unsigned char* s1 = (unsigned char*)s1V; \
      unsigned char* s2 = (unsigned char*)s2V; \
      \
      while (n != 0) { \
         a0 = s1[0]; \
         b0 = s2[0]; \
         s1 += 1; \
         s2 += 1; \
         res = ((int)a0) - ((int)b0); \
         if (res != 0) \
            return res; \
         n -= 1; \
      } \
      return 0; \
   }

MEMCMP(VG_Z_LIBC_SONAME, memcmp)
MEMCMP(VG_Z_LIBC_SONAME, bcmp)
#if defined(VGO_linux)
MEMCMP(VG_Z_LD_SO_1,     bcmp)
#elif defined(VGO_darwin)
MEMCMP(VG_Z_DYLD,        memcmp)
MEMCMP(VG_Z_DYLD,        bcmp)
#endif


/* Copy SRC to DEST, returning the address of the terminating '\0' in
   DEST. (minor variant of strcpy) */
#define STPCPY(soname, fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2020,soname,fnname) \
            ( char* dst, const char* src ); \
   char* VG_REPLACE_FUNCTION_EZU(2020,soname,fnname) \
            ( char* dst, const char* src ) \
   { \
      const Char* src_orig = src; \
            Char* dst_orig = dst; \
      \
      while (*src) *dst++ = *src++; \
      *dst = 0; \
      \
      /* This checks for overlap after copying, unavoidable without */ \
      /* pre-counting length... should be ok */ \
      if (is_overlap(dst_orig,  \
                     src_orig,  \
                     (Addr)dst-(Addr)dst_orig+1,  \
                     (Addr)src-(Addr)src_orig+1)) \
         RECORD_OVERLAP_ERROR("stpcpy", dst_orig, src_orig, 0); \
      \
      return dst; \
   }

STPCPY(VG_Z_LIBC_SONAME,          stpcpy)
#if defined(VGO_linux)
STPCPY(VG_Z_LIBC_SONAME,          __GI_stpcpy)
STPCPY(VG_Z_LD_LINUX_SO_2,        stpcpy)
STPCPY(VG_Z_LD_LINUX_X86_64_SO_2, stpcpy)
#elif defined(VGO_darwin)
STPCPY(VG_Z_DYLD,                 stpcpy)
#endif


#define MEMSET(soname, fnname) \
   void* VG_REPLACE_FUNCTION_EZU(2021,soname,fnname) \
            (void *s, Int c, SizeT n); \
   void* VG_REPLACE_FUNCTION_EZU(2021,soname,fnname) \
            (void *s, Int c, SizeT n) \
   { \
      Addr a  = (Addr)s;   \
      UInt c4 = (c & 0xFF); \
      c4 = (c4 << 8) | c4; \
      c4 = (c4 << 16) | c4; \
      while ((a & 3) != 0 && n >= 1) \
         { *(UChar*)a = (UChar)c; a += 1; n -= 1; } \
      while (n >= 4) \
         { *(UInt*)a = c4; a += 4; n -= 4; } \
      while (n >= 1) \
         { *(UChar*)a = (UChar)c; a += 1; n -= 1; } \
      return s; \
   }

MEMSET(VG_Z_LIBC_SONAME, memset)
#if defined(VGO_darwin)
MEMSET(VG_Z_DYLD,        memset)
#endif


/* memmove -- use the MEMMOVE defn which also serves for memcpy. */
MEMMOVE(VG_Z_LIBC_SONAME, memmove)
#if defined(VGO_darwin)
MEMMOVE(VG_Z_DYLD,        memmove)
#endif


#define BCOPY(soname, fnname) \
   void VG_REPLACE_FUNCTION_EZU(2023,soname,fnname) \
            (const void *srcV, void *dstV, SizeT n); \
   void VG_REPLACE_FUNCTION_EZU(2023,soname,fnname) \
            (const void *srcV, void *dstV, SizeT n) \
   { \
      SizeT i; \
      Char* dst = (Char*)dstV; \
      Char* src = (Char*)srcV; \
      if (dst < src) { \
         for (i = 0; i < n; i++) \
            dst[i] = src[i]; \
      } \
      else  \
      if (dst > src) { \
         for (i = 0; i < n; i++) \
            dst[n-i-1] = src[n-i-1]; \
      } \
   }

#if defined(VGO_darwin)
BCOPY(VG_Z_LIBC_SONAME, bcopy)
BCOPY(VG_Z_DYLD,        bcopy)
#endif


/* glibc 2.5 variant of memmove which checks the dest is big enough.
   There is no specific part of glibc that this is copied from. */
#define GLIBC25___MEMMOVE_CHK(soname, fnname) \
   void* VG_REPLACE_FUNCTION_EZU(2024,soname,fnname) \
            (void *dstV, const void *srcV, SizeT n, SizeT destlen); \
   void* VG_REPLACE_FUNCTION_EZU(2024,soname,fnname) \
            (void *dstV, const void *srcV, SizeT n, SizeT destlen) \
   { \
      SizeT i; \
      Char* dst = (Char*)dstV; \
      Char* src = (Char*)srcV; \
      if (destlen < n) \
         goto badness; \
      if (dst < src) { \
         for (i = 0; i < n; i++) \
            dst[i] = src[i]; \
      } \
      else  \
      if (dst > src) { \
         for (i = 0; i < n; i++) \
            dst[n-i-1] = src[n-i-1]; \
      } \
      return dst; \
     badness: \
      VALGRIND_PRINTF_BACKTRACE( \
         "*** memmove_chk: buffer overflow detected ***: " \
         "program terminated\n"); \
     my_exit(127); \
     /*NOTREACHED*/ \
     return NULL; \
   }

GLIBC25___MEMMOVE_CHK(VG_Z_LIBC_SONAME, __memmove_chk)


/* Find the first occurrence of C in S or the final NUL byte.  */
#define GLIBC232_STRCHRNUL(soname, fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2025,soname,fnname) \
            (const char* s, int c_in); \
   char* VG_REPLACE_FUNCTION_EZU(2025,soname,fnname) \
            (const char* s, int c_in) \
   { \
      unsigned char  c        = (unsigned char) c_in; \
      unsigned char* char_ptr = (unsigned char *)s; \
      while (1) { \
         if (*char_ptr == 0) return char_ptr; \
         if (*char_ptr == c) return char_ptr; \
         char_ptr++; \
      } \
   }

GLIBC232_STRCHRNUL(VG_Z_LIBC_SONAME, strchrnul)


/* Find the first occurrence of C in S.  */
#define GLIBC232_RAWMEMCHR(soname, fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2026,soname,fnname) \
            (const char* s, int c_in); \
   char* VG_REPLACE_FUNCTION_EZU(2026,soname,fnname) \
            (const char* s, int c_in) \
   { \
      unsigned char  c        = (unsigned char) c_in; \
      unsigned char* char_ptr = (unsigned char *)s; \
      while (1) { \
         if (*char_ptr == c) return char_ptr; \
         char_ptr++; \
      } \
   }

GLIBC232_RAWMEMCHR(VG_Z_LIBC_SONAME, rawmemchr)
#if defined (VGO_linux)
GLIBC232_RAWMEMCHR(VG_Z_LIBC_SONAME, __GI___rawmemchr)
#endif

/* glibc variant of strcpy that checks the dest is big enough.
   Copied from glibc-2.5/debug/test-strcpy_chk.c. */
#define GLIBC25___STRCPY_CHK(soname,fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2027,soname,fnname) \
            (char* dst, const char* src, SizeT len); \
   char* VG_REPLACE_FUNCTION_EZU(2027,soname,fnname) \
            (char* dst, const char* src, SizeT len) \
   { \
      char* ret = dst; \
      if (! len) \
         goto badness; \
      while ((*dst++ = *src++) != '\0') \
         if (--len == 0) \
            goto badness; \
      return ret; \
     badness: \
      VALGRIND_PRINTF_BACKTRACE( \
         "*** strcpy_chk: buffer overflow detected ***: " \
         "program terminated\n"); \
     my_exit(127); \
     /*NOTREACHED*/ \
     return NULL; \
   }

GLIBC25___STRCPY_CHK(VG_Z_LIBC_SONAME, __strcpy_chk)


/* glibc variant of stpcpy that checks the dest is big enough.
   Copied from glibc-2.5/debug/test-stpcpy_chk.c. */
#define GLIBC25___STPCPY_CHK(soname,fnname) \
   char* VG_REPLACE_FUNCTION_EZU(2028,soname,fnname) \
            (char* dst, const char* src, SizeT len); \
   char* VG_REPLACE_FUNCTION_EZU(2028,soname,fnname) \
            (char* dst, const char* src, SizeT len) \
   { \
      if (! len) \
         goto badness; \
      while ((*dst++ = *src++) != '\0') \
         if (--len == 0) \
            goto badness; \
      return dst - 1; \
     badness: \
      VALGRIND_PRINTF_BACKTRACE( \
         "*** stpcpy_chk: buffer overflow detected ***: " \
         "program terminated\n"); \
     my_exit(127); \
     /*NOTREACHED*/ \
     return NULL; \
   }

GLIBC25___STPCPY_CHK(VG_Z_LIBC_SONAME, __stpcpy_chk)


/* mempcpy */
#define GLIBC25_MEMPCPY(soname, fnname) \
   void* VG_REPLACE_FUNCTION_EZU(2029,soname,fnname) \
            ( void *dst, const void *src, SizeT len ); \
   void* VG_REPLACE_FUNCTION_EZU(2029,soname,fnname) \
            ( void *dst, const void *src, SizeT len ) \
   { \
      register char *d; \
      register char *s; \
      SizeT len_saved = len; \
      \
      if (len == 0) \
         return dst; \
      \
      if (is_overlap(dst, src, len, len)) \
         RECORD_OVERLAP_ERROR("mempcpy", dst, src, len); \
      \
      if ( dst > src ) { \
         d = (char *)dst + len - 1; \
         s = (char *)src + len - 1; \
         while ( len-- ) { \
            *d-- = *s--; \
         } \
      } else if ( dst < src ) { \
         d = (char *)dst; \
         s = (char *)src; \
         while ( len-- ) { \
            *d++ = *s++; \
         } \
      } \
      return (void*)( ((char*)dst) + len_saved ); \
   }

GLIBC25_MEMPCPY(VG_Z_LIBC_SONAME, mempcpy)
#if defined(VGO_linux)
GLIBC25_MEMPCPY(VG_Z_LD_SO_1,     mempcpy) /* ld.so.1 */
#endif


#define GLIBC26___MEMCPY_CHK(soname, fnname) \
   void* VG_REPLACE_FUNCTION_EZU(2030,soname,fnname) \
            (void* dst, const void* src, SizeT len, SizeT dstlen ); \
   void* VG_REPLACE_FUNCTION_EZU(2030,soname,fnname) \
            (void* dst, const void* src, SizeT len, SizeT dstlen ) \
   { \
      register char *d; \
      register char *s; \
      \
      if (dstlen < len) goto badness; \
      \
      if (len == 0) \
         return dst; \
      \
      if (is_overlap(dst, src, len, len)) \
         RECORD_OVERLAP_ERROR("memcpy_chk", dst, src, len); \
      \
      if ( dst > src ) { \
         d = (char *)dst + len - 1; \
         s = (char *)src + len - 1; \
         while ( len-- ) { \
            *d-- = *s--; \
         } \
      } else if ( dst < src ) { \
         d = (char *)dst; \
         s = (char *)src; \
         while ( len-- ) { \
            *d++ = *s++; \
         } \
      } \
      return dst; \
     badness: \
      VALGRIND_PRINTF_BACKTRACE( \
         "*** memcpy_chk: buffer overflow detected ***: " \
         "program terminated\n"); \
     my_exit(127); \
     /*NOTREACHED*/ \
     return NULL; \
   }

GLIBC26___MEMCPY_CHK(VG_Z_LIBC_SONAME, __memcpy_chk)


#define STRSTR(soname, fnname) \
   void* VG_REPLACE_FUNCTION_EZU(2031,soname,fnname) \
         (void* haystack, void* needle); \
   void* VG_REPLACE_FUNCTION_EZU(2031,soname,fnname) \
         (void* haystack, void* needle) \
   { \
      UChar* h = (UChar*)haystack; \
      UChar* n = (UChar*)needle; \
      \
      /* find the length of n, not including terminating zero */ \
      UWord nlen = 0; \
      while (n[nlen]) nlen++; \
      \
      /* if n is the empty string, match immediately. */ \
      if (nlen == 0) return h; \
      \
      /* assert(nlen >= 1); */ \
      UChar n0 = n[0]; \
      \
      while (1) { \
         UChar hh = *h; \
         if (hh == 0) return NULL; \
         if (hh != n0) { h++; continue; } \
         \
         UWord i; \
         for (i = 0; i < nlen; i++) { \
            if (n[i] != h[i]) \
               break; \
         } \
         /* assert(i >= 0 && i <= nlen); */ \
         if (i == nlen) \
            return h; \
         \
         h++; \
      } \
   }

#if defined(VGO_linux)
STRSTR(VG_Z_LIBC_SONAME,          strstr)
#endif


#define STRPBRK(soname, fnname) \
   void* VG_REPLACE_FUNCTION_EZU(2032,soname,fnname) \
         (void* sV, void* acceptV); \
   void* VG_REPLACE_FUNCTION_EZU(2032,soname,fnname) \
         (void* sV, void* acceptV) \
   { \
      UChar* s = (UChar*)sV; \
      UChar* accept = (UChar*)acceptV; \
      \
      /*  find the length of 'accept', not including terminating zero */ \
      UWord nacc = 0; \
      while (accept[nacc]) nacc++; \
      \
      /* if n is the empty string, fail immediately. */ \
      if (nacc == 0) return NULL; \
      \
      /* assert(nacc >= 1); */ \
      while (1) { \
         UWord i; \
         UChar sc = *s; \
         if (sc == 0) \
            break; \
         for (i = 0; i < nacc; i++) { \
            if (sc == accept[i]) \
               return s; \
         } \
         s++; \
      } \
      \
      return NULL; \
   }

#if defined(VGO_linux)
STRPBRK(VG_Z_LIBC_SONAME,          strpbrk)
#endif


#define STRCSPN(soname, fnname) \
   SizeT VG_REPLACE_FUNCTION_EZU(2033,soname,fnname) \
         (void* sV, void* rejectV); \
   SizeT VG_REPLACE_FUNCTION_EZU(2033,soname,fnname) \
         (void* sV, void* rejectV) \
   { \
      UChar* s = (UChar*)sV; \
      UChar* reject = (UChar*)rejectV; \
      \
      /* find the length of 'reject', not including terminating zero */ \
      UWord nrej = 0; \
      while (reject[nrej]) nrej++; \
      \
      UWord len = 0; \
      while (1) { \
         UWord i; \
         UChar sc = *s; \
         if (sc == 0) \
            break; \
         for (i = 0; i < nrej; i++) { \
            if (sc == reject[i]) \
               break; \
         } \
         /* assert(i >= 0 && i <= nrej); */ \
         if (i < nrej) \
            break; \
         s++; \
         len++; \
      } \
      \
      return len; \
   }

#if defined(VGO_linux)
STRCSPN(VG_Z_LIBC_SONAME,          strcspn)
#endif


#define STRSPN(soname, fnname) \
   SizeT VG_REPLACE_FUNCTION_EZU(2034,soname,fnname) \
         (void* sV, void* acceptV); \
   SizeT VG_REPLACE_FUNCTION_EZU(2034,soname,fnname) \
         (void* sV, void* acceptV) \
   { \
      UChar* s = (UChar*)sV; \
      UChar* accept = (UChar*)acceptV; \
      \
      /* find the length of 'accept', not including terminating zero */ \
      UWord nacc = 0; \
      while (accept[nacc]) nacc++; \
      if (nacc == 0) return 0; \
      \
      UWord len = 0; \
      while (1) { \
         UWord i; \
         UChar sc = *s; \
         if (sc == 0) \
            break; \
         for (i = 0; i < nacc; i++) { \
            if (sc == accept[i]) \
               break; \
         } \
         /* assert(i >= 0 && i <= nacc); */ \
         if (i == nacc) \
            break; \
         s++; \
         len++; \
      } \
      \
      return len; \
   }

#if defined(VGO_linux)
STRSPN(VG_Z_LIBC_SONAME,          strspn)
#endif


/*------------------------------------------------------------*/
/*--- Improve definedness checking of process environment  ---*/
/*------------------------------------------------------------*/

#if defined(VGO_linux)

/* If these wind up getting generated via a macro, so that multiple
   versions of each function exist (as above), use the _EZU variants
   to assign equivalance class tags. */

/* putenv */
int VG_WRAP_FUNCTION_ZU(VG_Z_LIBC_SONAME, putenv) (char* string);
int VG_WRAP_FUNCTION_ZU(VG_Z_LIBC_SONAME, putenv) (char* string)
{
    OrigFn fn;
    Word result;
    const char* p = string;
    VALGRIND_GET_ORIG_FN(fn);
    /* Now by walking over the string we magically produce
       traces when hitting undefined memory. */
    if (p)
        while (*p++)
            ;
    CALL_FN_W_W(result, fn, string);
    return result;
}

/* unsetenv */
int VG_WRAP_FUNCTION_ZU(VG_Z_LIBC_SONAME, unsetenv) (const char* name);
int VG_WRAP_FUNCTION_ZU(VG_Z_LIBC_SONAME, unsetenv) (const char* name)
{
    OrigFn fn;
    Word result;
    const char* p = name;
    VALGRIND_GET_ORIG_FN(fn);
    /* Now by walking over the string we magically produce
       traces when hitting undefined memory. */
    if (p)
        while (*p++)
            ;
    CALL_FN_W_W(result, fn, name);
    return result;
}

/* setenv */
int VG_WRAP_FUNCTION_ZU(VG_Z_LIBC_SONAME, setenv)
    (const char* name, const char* value, int overwrite);
int VG_WRAP_FUNCTION_ZU(VG_Z_LIBC_SONAME, setenv)
    (const char* name, const char* value, int overwrite)
{
    OrigFn fn;
    Word result;
    const char* p;
    VALGRIND_GET_ORIG_FN(fn);
    /* Now by walking over the string we magically produce
       traces when hitting undefined memory. */
    if (name)
        for (p = name; *p; p++)
            ;
    if (value)
        for (p = value; *p; p++)
            ;
    VALGRIND_CHECK_VALUE_IS_DEFINED (overwrite);
    CALL_FN_W_WWW(result, fn, name, value, overwrite);
    return result;
}

#endif /* defined(VGO_linux) */

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
