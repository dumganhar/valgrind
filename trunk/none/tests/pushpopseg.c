#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
  unsigned long  sp1;
  unsigned long  sp2;
  unsigned long  sp3;
  unsigned short fs1;
  unsigned short fs2;

  fs1 = 0x0003;
  
  asm("movw %4, %%fs\n"
      "movl %%esp, %0\n"
      "pushw %%fs\n"
      "movl %%esp, %1\n"
      "popw %%fs\n"
      "movl %%esp, %2\n"
      "movw %%fs, %3\n"
      : "=m" (sp1), "=m" (sp2), "=m" (sp3), "=m" (fs2)
      : "m" (fs1)
      : "ax"
      );

  printf("sp change after push = %d\n", sp2 - sp1);
  printf("sp change after pop = %d\n", sp3 - sp2);
  printf("fs after push and pop = %04x\n", fs1);
   
  asm("movw %4, %%fs\n"
      "movl %%esp, %0\n"
      "pushl %%fs\n"
      "movl %%esp, %1\n"
      "popl %%fs\n"
      "movl %%esp, %2\n"
      "movw %%fs, %3\n"
      : "=m" (sp1), "=m" (sp2), "=m" (sp3), "=m" (fs2)
      : "m" (fs1)
      : "ax"
      );

  printf("sp change after push = %d\n", sp2 - sp1);
  printf("sp change after pop = %d\n", sp3 - sp2);
  printf("fs after push and pop = %04x\n", fs1);
 
  exit(0);
}
