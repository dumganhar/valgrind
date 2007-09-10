
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* Simple test program, no race.  Parent and child both modify x and
   use the hardware bus lock. */

#undef PLAT_x86_linux
#undef PLAT_amd64_linux
#undef PLAT_ppc32_linux
#undef PLAT_ppc64_linux
#undef PLAT_ppc32_aix5
#undef PLAT_ppc64_aix5

#if !defined(_AIX) && defined(__i386__)
#  define PLAT_x86_linux 1
#elif !defined(_AIX) && defined(__x86_64__)
#  define PLAT_amd64_linux 1
#elif !defined(_AIX) && defined(__powerpc__) && !defined(__powerpc64__)
#  define PLAT_ppc32_linux 1
#elif !defined(_AIX) && defined(__powerpc__) && defined(__powerpc64__)
#  define PLAT_ppc64_linux 1
#elif defined(_AIX) && defined(__64BIT__)
#  define PLAT_ppc64_aix5 1
#elif defined(_AIX) && !defined(__64BIT__)
#  define PLAT_ppc32_aix5 1
#endif


#if defined(PLAT_amd64_linux)
#  define INC(_lval) \
      __asm__ __volatile__ ( \
      "lock ; incl (%0)" : /*out*/ : /*in*/"r"(&(_lval)) : "memory", "cc" )
#else
#  error "Fix Me for this platform"
#endif


int x = 0;

void* child_fn ( void* arg )
{
   INC(x);
   return NULL;
}

int main ( void )
{
   pthread_t child;

   if (pthread_create(&child, NULL, child_fn, NULL)) {
      perror("pthread_create");
      exit(1);
   }

   INC(x);

   if (pthread_join(child, NULL)) {
      perror("pthread join");
      exit(1);
   }

   printf("x = %d\n", x);
   return 0;
}
