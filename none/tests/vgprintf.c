#include "valgrind.h"
#include <stdio.h>

int
main (int argc, char **argv)
{
   int x = VALGRIND_PRINTF("Yo\n");
   printf ("%d\n", x);
   return 0;
}
