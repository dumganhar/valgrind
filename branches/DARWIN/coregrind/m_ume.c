
/*--------------------------------------------------------------------*/
/*--- User-mode execve(), and other stuff shared between stage1    ---*/
/*--- and stage2.                                          m_ume.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2008 Julian Seward 
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

#include "priv_ume.h"


typedef struct {
   const HChar *name;
   Bool (*match_fn)(char *hdr, Int len);
   Int  (*load_fn)(Int fd, const HChar *name, ExeInfo *info);
} ExeHandler;

static ExeHandler exe_handlers[] = {
#if defined(HAVE_ELF)
   { "ELF",    VG_(match_ELF),    VG_(load_ELF) },
#endif
#if defined(HAVE_SCRIPT)
   { "script", VG_(match_script), VG_(load_script) },
#endif
#if defined(HAVE_MACHO)
   { "Mach-O", VG_(match_macho),  VG_(load_macho) },
#endif
};
#define EXE_HANDLER_COUNT (sizeof(exe_handlers)/sizeof(exe_handlers[0]))


// Check the file looks executable.
SysRes VG_(pre_exec_check)(const HChar* exe_name, Int* out_fd, Bool allow_setuid)
{
   Int fd, ret, i;
   SysRes res;
   Char  buf[4096];
   SizeT bufsz = 4096, fsz;
   Bool is_setuid = False;

   // Check it's readable
   res = VG_(open)(exe_name, VKI_O_RDONLY, 0);
   if (res.isError) {
      return res;
   }
   fd = res.res;

   // Check we have execute permissions
   ret = VG_(check_executable)(&is_setuid, (HChar*)exe_name, allow_setuid);
   if (0 != ret) {
      VG_(close)(fd);
      if (is_setuid && !VG_(clo_xml)) {
         VG_(message)(Vg_UserMsg, "");
         VG_(message)(Vg_UserMsg,
                      "Warning: Can't execute setuid/setgid executable: %s",
                      exe_name);
         VG_(message)(Vg_UserMsg, "Possible workaround: remove "
                      "--trace-children=yes, if in effect");
         VG_(message)(Vg_UserMsg, "");
      }
      return VG_(mk_SysRes_Error)(ret);
   }

   fsz = (SizeT)VG_(fsize)(fd);
   if (fsz < bufsz)
      bufsz = fsz;

   res = VG_(pread)(fd, buf, bufsz, 0);
   if (res.isError || res.res != bufsz) {
      VG_(close)(fd);
      return VG_(mk_SysRes_Error)(VKI_EACCES);
   }
   bufsz = res.res;

   // Look for a matching executable format
   for (i = 0; i < EXE_HANDLER_COUNT; i++) {
      if ((*exe_handlers[i].match_fn)(buf, bufsz)) {
         res = VG_(mk_SysRes_Success)(i);
         break;
      }
   }
   if (i == EXE_HANDLER_COUNT) {
      // Rejected by all executable format handlers.
      res = VG_(mk_SysRes_Error)(VKI_ENOEXEC);
   }

   // Write the 'out_fd' param if necessary, or close the file.
   if (!res.isError && out_fd) {
      *out_fd = fd; 
   } else { 
      VG_(close)(fd);
   }

   return res;
}


// returns: 0 = success, non-0 is failure
//
// We can execute only ELF or Mach-O binaries or scripts that begin with "#!".
// (Not, for example, scripts that don't begin with "#!";  see the 
// VG_(do_exec)() invocation from m_main.c for how that's handled.)
Int VG_(do_exec_inner)(const HChar *exe, ExeInfo *info)
{
   SysRes res;
   Int fd;
   Int ret;

   res = VG_(pre_exec_check)(exe, &fd, False/*allow_setuid*/);
   if (res.isError)
      return res.err;

   vg_assert2(res.res >= 0  &&  res.res < EXE_HANDLER_COUNT, 
              "invalid VG_(pre_exec_check) result");

   ret = (*exe_handlers[res.res].load_fn)(fd, exe, info);

   VG_(close)(fd);

   return ret;
}


static Bool is_hash_bang_file(const Char* f)
{
   SysRes res = VG_(open)(f, VKI_O_RDONLY, 0);
   if (!res.isError) {
      Char buf[3] = {0,0,0};
      Int fd = res.res;
      Int n  = VG_(read)(fd, buf, 2); 
      if (n == 2 && VG_STREQ("#!", buf))
         return True;
   }
   return False;
}

// Look at the first 80 chars, and if any are greater than 127, it's binary.
// This is crude, but should be good enough.  Note that it fails on a
// zero-length file, as we want.
static Bool is_binary_file(const Char* f)
{
   SysRes res = VG_(open)(f, VKI_O_RDONLY, 0);
   if (!res.isError) {
      UChar buf[80];
      Int fd = res.res;
      Int n  = VG_(read)(fd, buf, 80); 
      Int i;
      for (i = 0; i < n; i++) {
         if (buf[i] > 127)
            return True;      // binary char found
      }
      return False;
   } else {
      // Something went wrong.  This will only happen if we earlier
      // succeeded in opening the file but fail here (eg. the file was
      // deleted between then and now).
      VG_(printf)("valgrind: %s: unknown error\n", f);
      VG_(exit)(126);      // 126 == NOEXEC
   }
}

// If the do_exec fails we try to emulate what the shell does (I used
// bash as a guide).  It's worth noting that the shell can execute some
// things that VG_(do_exec)() (which subsitutes for the kernel's exec())
// will refuse to (eg. scripts lacking a "#!" prefix).
static Int do_exec_shell_followup(Int ret, const HChar* exe_name,
                                  ExeInfo* info)
{
   Char*  default_interp_name = "/bin/sh";
   SysRes res;
   struct vg_stat st;

   if (VKI_ENOEXEC == ret) {
      // It was an executable file, but in an unacceptable format.  Probably
      // is a shell script lacking the "#!" prefix;  try to execute it so.

      // Is it a binary file?  
      if (is_binary_file(exe_name)) {
         VG_(printf)("valgrind: %s: cannot execute binary file\n", exe_name);
         VG_(exit)(126);      // 126 == NOEXEC
      }

      // Looks like a script.  Run it with /bin/sh.  This includes
      // zero-length files.

      info->interp_name = VG_(strdup)("ume.desf.1", default_interp_name);
      info->interp_args = NULL;
      if (info->argv && info->argv[0] != NULL)
         info->argv[0] = (char *)exe_name;

      ret = VG_(do_exec_inner)(info->interp_name, info);

      if (0 != ret) {
         // Something went wrong with executing the default interpreter
         VG_(printf)("valgrind: %s: bad interpreter (%s): %s\n",
                     exe_name, info->interp_name, VG_(strerror)(ret));
         VG_(exit)(126);      // 126 == NOEXEC
      }

   } else if (0 != ret) {
      // Something else went wrong.  Try to make the error more specific,
      // and then print a message and abort.
   
      // Was it a directory?
      res = VG_(stat)(exe_name, &st);
      if (!res.isError && VKI_S_ISDIR(st.mode)) {
         VG_(printf)("valgrind: %s: is a directory\n", exe_name);
      
      // Was it not executable?
      } else if (0 != VG_(check_executable)(NULL, exe_name, 
                                            False/*allow_setuid*/)) {
         VG_(printf)("valgrind: %s: %s\n", exe_name, VG_(strerror)(ret));

      // Did it start with "#!"?  If so, it must have been a bad interpreter.
      } else if (is_hash_bang_file(exe_name)) {
         VG_(printf)("valgrind: %s: bad interpreter: %s\n",
                     exe_name, VG_(strerror)(ret));

      // Otherwise it was something else.
      } else {
         VG_(printf)("valgrind: %s: %s\n", exe_name, VG_(strerror)(ret));
      }
      // 126 means NOEXEC;  I think this is Posix, and that in some cases we
      // should be returning 127, meaning NOTFOUND.  Oh well.
      VG_(exit)(126);
   }
   return ret;
}


// This emulates the kernel's exec().  If it fails, it then emulates the
// shell's handling of the situation.
// See ume.h for an indication of which entries of 'info' are inputs, which
// are outputs, and which are both.
/* returns: 0 = success, non-0 is failure */
Int VG_(do_exec)(const HChar* exe_name, ExeInfo* info)
{
   Int ret;
   
   info->interp_name = NULL;
   info->interp_args = NULL;

   ret = VG_(do_exec_inner)(exe_name, info);

   if (0 != ret) {
      ret = do_exec_shell_followup(ret, exe_name, info);
   }
   return ret;
}


/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
