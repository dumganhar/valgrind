
#include <stdio.h>
#include "valgrind.h"

/* Check that function wrapping works for a recursive function. */

/* This is needed to stop gcc4 turning 'fact' into a loop */
__attribute__((noinline))
int mul ( int x, int y ) { return x * y; }

int fact ( int n )
{
   if (n == 0) return 1; else return mul(n, fact(n-1));
}

int I_REPLACE_SONAME_FNNAME_ZU(NONE,fact) ( int n )
{
   int r;
   printf("in wrapper1-pre:  fact(%d)\n", n);
   CALL_ORIG_FN_1(r,fact,n);
   printf("in wrapper1-post: fact(%d) = %d\n", n, r);
   return r;
}

/* --------------- */

int main ( void )
{
   int r;
   printf("computing fact(5)\n");
   r = fact(5);
   printf("fact(5) = %d\n", r);
   return 0;
}
