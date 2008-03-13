/** Test whether detached threads are handled properly.
 *  Copyright (c) 2006-2008 by Bart Van Assche (bart.vanassche@gmail.com).
 */


#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../drd_clientreq.h"


static int   s_set_thread_name;
static sem_t s_sem;


static void set_thread_name(const char* const fmt, const int arg)
{
  if (s_set_thread_name)
  {
    int res;
    char name[32];
    snprintf(name, sizeof(name), fmt, arg);
    name[sizeof(name) - 1] = 0;
    VALGRIND_DO_CLIENT_REQUEST(res, 0, VG_USERREQ__SET_THREAD_NAME,
                               name, 0, 0, 0, 0);
  }
}

static void increment_finished_count()
{
  sem_post(&s_sem);
}

static void* thread_func1(void* arg)
{
  set_thread_name("thread_func1[%d]", *(int*)arg);
  write(STDOUT_FILENO, ".", 1);
  increment_finished_count();
  return 0;
}

static void* thread_func2(void* arg)
{
  set_thread_name("thread_func2[%d]", *(int*)arg);
  pthread_detach(pthread_self());
  write(STDOUT_FILENO, ".", 1);
  increment_finished_count();
  return 0;
}

int main(int argc, char** argv)
{
  const int count1 = argc > 1 ? atoi(argv[1]) : 100;
  const int count2 = argc > 2 ? atoi(argv[2]) : 100;
  const int do_set_thread_name = argc > 3 ? atoi(argv[3]) != 0 : 0;
  int thread_arg[count1 > count2 ? count1 : count2];
  int i;
  int detachstate;
  pthread_attr_t attr;

  s_set_thread_name = do_set_thread_name;

  set_thread_name("main", 0);

  for (i = 0; i < count1 || i < count2; i++)
    thread_arg[i] = i;

  sem_init(&s_sem, 0, 0);
  
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  assert(pthread_attr_getdetachstate(&attr, &detachstate) == 0);
  assert(detachstate == PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&attr, 16384);
  // Create count1 detached threads by setting the "detached" property via 
  // thread attributes.
  for (i = 0; i < count1; i++)
  {
    pthread_t thread;
    pthread_create(&thread, &attr, thread_func1, &thread_arg[i]);
  }
  // Create count2 detached threads by letting the threads detach themselves.
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  assert(pthread_attr_getdetachstate(&attr, &detachstate) == 0);
  assert(detachstate == PTHREAD_CREATE_JOINABLE);
  for (i = 0; i < count2; i++)
  {
    pthread_t thread;
    pthread_create(&thread, &attr, thread_func2, &thread_arg[i]);
  }
  pthread_attr_destroy(&attr);

  // Wait until all detached threads have written their output to stdout.
  for (i = 0; i < count1 + count2; i++)
  {
    sem_wait(&s_sem);
  }

  write(STDOUT_FILENO, "\n", 1);

  sem_destroy(&s_sem);

  return 0;
}
