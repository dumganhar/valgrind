
/* This really exists to check that Thrcheck behaves plausibly
   with pthread_once calls.  Which it appears to.

   The original source of this program is as shown below, although it
   has been modified somewhat.  See
   http://www.oreilly.com/pub/a/oreilly/ask_tim/2001/codepolicy.html
   for OReilly's policy on using bits of their code examples.
*/


/********************************************************
 * An example source module to accompany...
 *
 * "Using POSIX Threads: Programming with Pthreads"
 *     by Brad Nichols, Dick Buttlar, Jackie Farrell
 *     O'Reilly & Associates, Inc.
 *
 ********************************************************
 * once_exam.c
 *
 * An example of using the pthreads_once() call to execute an
 * initialization procedure.
 *
 * A program spawns multiple threads and each one tries to
 * execute the routine welcome() using the once call. Only
 * the first thread into the once routine will actually
 * execute welcome().
 *
 * The program's main thread synchronizes its exit with the
 * exit of the threads using the pthread_join() operation.
 *
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include <pthread.h>

#define  NUM_THREADS 4

static pthread_once_t welcome_once_block = PTHREAD_ONCE_INIT;

static int unprotected1 = 0;
static int unprotected2 = 0;

void welcome(void) {
   printf("welcome: Welcome\n");
   unprotected1++; /* this is harmless */
}

void* child ( void* argV ) {
   int r= pthread_once(&welcome_once_block, welcome); assert(!r);
   printf("child: Hi, I'm thread %d\n", *(int*)argV);
   unprotected2++; /* whereas this is a race */
   return NULL;
}

int main ( void ) {
   int       *id_arg, i, r;
   pthread_t threads[NUM_THREADS];

   id_arg = (int *)malloc(NUM_THREADS*sizeof(int));

   for (i = 0; i < NUM_THREADS; i++) {
      id_arg[i] = i;
      r= pthread_create(&threads[i], NULL, child, &id_arg[i]);
      assert(!r);
   }

   for (i = 0; i < NUM_THREADS; i++) {
      pthread_join(threads[i], NULL);
      //printf("main: joined to thread %d\n", i);
   }
   printf("main: Goodbye\n");
   return 0;
}
