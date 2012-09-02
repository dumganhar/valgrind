#include <stdio.h>
#include "opcodes.h"

/* Test that emulation warnings appear when the floating point
   extension facility is not installed and the opcode specifies
   a rounding mode other than 0. */
int
main(void)
{
   printf("before\n");
   __asm__ volatile ( CFEBRA(5,0,0,0) : : : "cc", "memory");
   __asm__ volatile ( CEFBRA(5,0,0,0) : : : "cc", "memory");
   __asm__ volatile ( CXFBRA(5,0,0,0) : : : "cc", "memory");
   __asm__ volatile ( CGXBRA(5,0,0,0) : : : "cc", "memory");

   printf("after\n");
   return 0;
}
