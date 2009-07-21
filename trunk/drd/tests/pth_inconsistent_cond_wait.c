/** Trigger two kinds of errors: once that condition variable s_cond is
 *  associated with two different mutexes (s_mutex1 and s_mutex2), and two
 *  times that pthread_cond_signal() is called without that the mutex
 *  associated with the condition variable is locked.
 */


#include <errno.h>     // ETIMEDOUT
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>    // memset()
#include <sys/time.h>  // gettimeofday()
#include <time.h>      // struct timespec
#include <unistd.h>

pthread_cond_t  s_cond;
pthread_mutex_t s_mutex1;
pthread_mutex_t s_mutex2;
sem_t           s_sem;

static void* thread_func(void* mutex)
{
  int err;
  struct timeval now;
  struct timespec deadline;

  pthread_mutex_lock(mutex);
  sem_post(&s_sem);
  gettimeofday(&now, 0);
  memset(&deadline, 0, sizeof(deadline));
  deadline.tv_sec  = now.tv_sec + 2;
  deadline.tv_nsec = now.tv_usec * 1000;
  err = pthread_cond_timedwait(&s_cond, mutex, &deadline);
  if (err != 0)
    fprintf(stderr,
	    "pthread_cond_timedwait() call returned error code %d (%s)\n",
	    err,
	    strerror(err));
  pthread_mutex_unlock(mutex);
  return 0;
}

int main(int argc, char** argv)
{
  pthread_t tid1;
  pthread_t tid2;

  /* Initialize synchronization objects. */
  sem_init(&s_sem, 0, 0);
  pthread_cond_init(&s_cond, 0);
  pthread_mutex_init(&s_mutex1, 0);
  pthread_mutex_init(&s_mutex2, 0);

  /* Create two threads. */
  pthread_create(&tid1, 0, &thread_func, &s_mutex1);
  pthread_create(&tid2, 0, &thread_func, &s_mutex2);

  /* Wait until both threads have called sem_post(). */
  sem_wait(&s_sem);
  sem_wait(&s_sem);

  /* Wait until both threads are waiting inside pthread_cond_wait(). */
  pthread_mutex_lock(&s_mutex1);
  pthread_mutex_lock(&s_mutex2);
  pthread_mutex_unlock(&s_mutex2);
  pthread_mutex_unlock(&s_mutex1);

  /* Signal s_cond twice. */
  pthread_cond_signal(&s_cond);
  pthread_cond_signal(&s_cond);

  /* Join both threads. */
  pthread_join(tid1, 0);
  pthread_join(tid2, 0);

  return 0;
}
