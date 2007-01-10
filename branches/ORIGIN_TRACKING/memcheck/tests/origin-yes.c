// This test covers all the different sources of values, both defined and
// undefined.  It only involves undefined condition errors.
//
// Nb: a stack frame is allocated when a signal is delivered.  But it
// immediately get written with stuff, so there's no significant possibility
// of undefined values originating there.  So we ignore it.  (On platforms
// like AMD64 that have a redzone just beyond the stack pointer there is a
// possibility, but it's so slim we ignore it.)

#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "memcheck.h"

int x = 0;

int main(void)
{
   assert(1 == sizeof(char));
   assert(2 == sizeof(short));
   assert(4 == sizeof(int));
   assert(8 == sizeof(long long));

   //------------------------------------------------------------------------
   // Sources of undefined values
   //------------------------------------------------------------------------

   // Stack, 32-bit
   {
      int undef_stack_int;
      x += (undef_stack_int == 0x12345678 ? 10 : 21);
   }
   
   // Stack, 32-bit, recently modified
   // XXX: this should work, as the unmodified version should be visible
   // within the IRSB.  Same for the next two cases -- what's going wrong?
   {
      int modified_undef_stack_int;
      modified_undef_stack_int++;
      x += (modified_undef_stack_int == 0x1234 ? 11 : 22);
   }
   
   // Stack, 16-bit from (recently) 32-bit
   {
      int undef_stack_int;
      short undef_stack_short = (short)undef_stack_int;
      x += (undef_stack_short == 0x1234 ? 11 : 22);
   }
   
   // Stack, 8-bit from (recently) 32-bit
   {
      int undef_stack_int;
      char undef_stack_char = (char)undef_stack_int;
      x += (undef_stack_char == 0x12 ? 11 : 22);
   }
   
   // Stack, 64-bit
   {
      long long undef_stack_longlong;
      x += (undef_stack_longlong == 0x1234567812345678LL ? 11 : 22);
   }
   
   // Malloc block, uninitialised, 32-bit
   {
      int* ptr_to_undef_malloc_int = malloc(sizeof(int));
      int  undef_malloc_int = *ptr_to_undef_malloc_int;
      x += (undef_malloc_int == 0x12345678 ? 12 : 23);
   }

   // Realloc block, uninitialised
   {
      int* ptr_to_undef_malloc_int2 = malloc(sizeof(int));
         // Allocate a big chunk to ensure that a new block is allocated.
      int* ptr_to_undef_realloc_int = realloc(ptr_to_undef_malloc_int2, 4096);
         // Have to move past the first 4 bytes, which were copied from the
         // malloc'd block.
      int  undef_realloc_int = *(ptr_to_undef_realloc_int+1);
      x += (undef_realloc_int == 0x12345678 ? 13 : 24);
   }

   // Custom-allocated block, non-zeroed
   {
      int  undef_custom_alloc_int;
      VALGRIND_MALLOCLIKE_BLOCK(&undef_custom_alloc_int, sizeof(int),
                                /*rzB*/0, /*is_zeroed*/0);
      x += (undef_custom_alloc_int == 0x12345678 ? 14 : 25);
   }

   // Heap segment (brk), uninitialised
   {
      int* ptr_to_new_brk_limit = sbrk(4096);
      int  undef_brk_int = *ptr_to_new_brk_limit;
      x += (undef_brk_int == 0x12345678 ? 15 : 26);
   }

   // User block, marked as undefined
   {
      int  undef_user_int = 0;
      VALGRIND_MAKE_MEM_UNDEFINED(&undef_user_int, sizeof(int));
      x += (undef_user_int == 0x12345678 ? 16 : 27);
   }

   //------------------------------------------------------------------------
   // Sources of defined values
   //------------------------------------------------------------------------

   // Heap block (calloc), initialised
   {
      int* ptr_to_def_calloc_int = calloc(1, sizeof(int));
      int  def_calloc_int = *ptr_to_def_calloc_int;
      x += (def_calloc_int == 0x12345678 ? 17 : 28);
   }

   // Custom-allocated block, non-zeroed
   {
      int  def_custom_alloc_int = 0;
      VALGRIND_MALLOCLIKE_BLOCK(&def_custom_alloc_int, sizeof(int),
                                /*rzB*/0, /*is_zeroed*/1);
      x += (def_custom_alloc_int == 0x12345678 ? 18 : 29);
   }

   // mmap block, initialised
   {
      int* ptr_to_def_mmap_int =
               mmap(0, 4096, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
      int def_mmap_int = *ptr_to_def_mmap_int;
      x += (def_mmap_int == 0x12345678 ? 19 : 30);
   }

   return x;
}
