
#include <stdio.h>
#include <malloc.h>

// Do a test comparison.  By default memcheck does not use the
// expensive EQ/NE scheme as it would be too expensive.  The 
// assignment to *hack is a trick to fool memcheck's bogus-literal
// spotter into thinking this is a bb which needs unusually careful
// attention, and therefore the expensive EQ/NE scheme is used.

__attribute__((noinline)) // keep your grubby hands off this fn
void foo ( int* p1, int* p2, unsigned int * hack )
{
  *hack = 0x80808080;
  if (*p1 == *p2)
    printf("foo\n");
  else
    printf("bar\n");
}


int main ( void )
{

  unsigned int hack;

  int* junk1 = malloc(sizeof(int));
  int* junk2 = malloc(sizeof(int));

  short* ps1 = (short*)junk1;
  short* ps2 = (short*)junk2;

  int*   pi1 = (int*)junk1;
  int*   pi2 = (int*)junk2;

  // both words completely undefined.  This should give an error.
  foo(pi1,pi2, &hack);

  // set half of the words, but to different values; so this should
  // not give an error, since inspection of the defined parts 
  // shows the two values are not equal, and so the definedness of
  // the conclusion is unaffected by the undefined halves.
  *ps1 = 41;
  *ps2 = 42;
  foo(pi1,pi2, &hack);

  // set half of the words, but to the same value, so this forces the
  // result of the comparison to depend on the undefined halves.
  // should give an error
  *ps1 = 42;
  *ps2 = 42;
  foo(pi1,pi2, &hack);

  return 0;
}
