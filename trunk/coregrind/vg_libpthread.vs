  GLIBC_2.0 {
    pthread_join; pthread_self; pthread_equal;
    pthread_exit; pthread_detach;

    pthread_getschedparam; pthread_setschedparam;

    pthread_attr_destroy;
    pthread_attr_getdetachstate; pthread_attr_setdetachstate;
    pthread_attr_getschedparam; pthread_attr_setschedparam;
    pthread_attr_getschedpolicy; pthread_attr_setschedpolicy;
    pthread_attr_getinheritsched; pthread_attr_setinheritsched;
    pthread_attr_getscope; pthread_attr_setscope;

    pthread_mutex_init; pthread_mutex_destroy;
    pthread_mutex_lock; pthread_mutex_trylock; pthread_mutex_unlock;

    pthread_mutexattr_init; pthread_mutexattr_destroy;

    # Don't version these, because it doesn't matter for Valgrind's libpthread
    #pthread_cond_init; pthread_cond_destroy;
    #pthread_cond_wait; pthread_cond_timedwait;
    #pthread_cond_signal; pthread_cond_broadcast;

    pthread_condattr_destroy; pthread_condattr_init;

    pthread_cancel; pthread_testcancel;
    pthread_setcancelstate; pthread_setcanceltype;

    pthread_sigmask; pthread_kill;

    pthread_key_create; pthread_key_delete;
    pthread_getspecific; pthread_setspecific;

    pthread_once;

    pthread_atfork;

    flockfile; funlockfile; ftrylockfile;

    # Non-standard POSIX1.x functions.
    pthread_mutexattr_getkind_np; pthread_mutexattr_setkind_np;

    # Protected names for functions used in other shared objects.
    __pthread_mutex_init; __pthread_mutex_destroy;
    __pthread_mutex_lock; __pthread_mutex_trylock; __pthread_mutex_unlock;
    __pthread_mutexattr_init; __pthread_mutexattr_destroy;
    __pthread_mutexattr_settype;
    __pthread_key_create; __pthread_getspecific; __pthread_setspecific;
    __pthread_once; __pthread_atfork;
    _IO_flockfile; _IO_ftrylockfile; _IO_funlockfile;

    # Hidden entry point (through macros).
    #_pthread_cleanup_pop; _pthread_cleanup_pop_restore; _pthread_cleanup_push;
    #_pthread_cleanup_push_defer;

    # Semaphores.
    #sem_destroy; sem_getvalue; sem_init; sem_post; sem_trywait; sem_wait;

    # Special fork handling.
    fork; __fork; vfork;

    # Cancellation points.
    close; __close; fcntl; __fcntl; read; __read; write; __write; accept;
    connect; __connect; recv; recvfrom; recvmsg; send; __send; sendmsg; sendto;
    fsync; lseek; __lseek; msync; nanosleep; open; __open; pause; tcdrain;
    system; wait; __wait; waitpid;

    # Hidden entry point (through macros).
    _pthread_cleanup_push; _pthread_cleanup_pop;
    _pthread_cleanup_push_defer; _pthread_cleanup_pop_restore;

    pthread_kill_other_threads_np;

    # The error functions.
    __errno_location; __h_errno_location;

    # Functions which previously have been overwritten.
    sigwait; sigaction; __sigaction; _exit; _Exit; longjmp; siglongjmp;
    raise;
  };

  GLIBC_2.1 {
    pthread_create;
    pthread_attr_init;

    pthread_attr_getguardsize; pthread_attr_setguardsize;
    pthread_attr_getstackaddr; pthread_attr_setstackaddr;
    pthread_attr_getstacksize; pthread_attr_setstacksize;

    pthread_mutexattr_gettype; pthread_mutexattr_settype;

    pthread_rwlock_init; pthread_rwlock_destroy;
    pthread_rwlock_rdlock; pthread_rwlock_wrlock; pthread_rwlock_unlock;
    pthread_rwlock_tryrdlock; pthread_rwlock_trywrlock;

    pthread_rwlockattr_init; pthread_rwlockattr_destroy;
    pthread_rwlockattr_getpshared; pthread_rwlockattr_setpshared;
    pthread_rwlockattr_getkind_np; pthread_rwlockattr_setkind_np;

    pthread_getconcurrency; pthread_setconcurrency;

    # Semaphores.
    sem_destroy; sem_getvalue; sem_init; sem_post; sem_trywait; sem_wait;

    __libc_current_sigrtmin; __libc_current_sigrtmax;
    __libc_allocate_rtsig;
  } GLIBC_2.0;

  GLIBC_2.1.1 {
    sem_close; sem_open; sem_unlink;
  } GLIBC_2.1;

  GLIBC_2.1.2 {
    __vfork;
  } GLIBC_2.1.1;

  GLIBC_2.2 {
    pthread_mutexattr_getpshared; pthread_mutexattr_setpshared;

    pthread_condattr_getpshared; pthread_condattr_setpshared;

    # New functions from IEEE Std. 1003.1-2001.
    pthread_mutex_timedlock;

    pthread_rwlock_timedrdlock; pthread_rwlock_timedwrlock;

    pthread_attr_getstack; pthread_attr_setstack;

    pthread_spin_destroy; pthread_spin_init; pthread_spin_lock;
    pthread_spin_trylock; pthread_spin_unlock;

    pthread_barrier_init; pthread_barrier_destroy; pthread_barrier_wait;
    pthread_barrierattr_destroy; pthread_barrierattr_init;
    pthread_barrierattr_setpshared;

    sem_timedwait;

    pthread_yield;

    pthread_getcpuclockid;

    # Cancellation points.
    lseek64; open64; __open64; pread; pread64; __pread64; pwrite; pwrite64;
    __pwrite64;

    # Names used internally.
    __pthread_rwlock_init; __pthread_rwlock_destroy;
    __pthread_rwlock_rdlock; __pthread_rwlock_tryrdlock;
    __pthread_rwlock_wrlock; __pthread_rwlock_trywrlock;
    __pthread_rwlock_unlock;

    __res_state;
  } GLIBC_2.1.2;

  GLIBC_2.2.3 {
    # Extensions.
    pthread_getattr_np;
    # These are in GLIBC_PRIVATE in the real library now but keep them 
    # here for now for backwards compatibiltiy - they will still be
    # visible to programs linked agianst newer libraries because of
    # the inheritance into GLIB_PRIVATE in this library.
    __pthread_clock_gettime; __pthread_clock_settime;
  } GLIBC_2.2;

  GLIBC_2.2.6 {
    # Cancellation wrapper
    __nanosleep;
  } GLIBC_2.2.3;

  GLIBC_2.3.2 {
    # Changed pthread_cond_t.
    # Don't version these, because it doesn't matter for Valgrind's libpthread
    #pthread_cond_init; pthread_cond_destroy;
    #pthread_cond_wait; pthread_cond_timedwait;
    #pthread_cond_signal; pthread_cond_broadcast;
  } GLIBC_2.2.6;

  GLIBC_2.3.3 {
    # 1003.1-2001 function accidentally left out in 2.2.
    pthread_barrierattr_getpshared;

    # Unix CS option.
    pthread_condattr_getclock; pthread_condattr_setclock;

    # Proposed API extensions.
    pthread_tryjoin_np; pthread_timedjoin_np;

    # New cancellation cleanup handling.
    __pthread_register_cancel; __pthread_unregister_cancel;
    __pthread_register_cancel_defer; __pthread_unregister_cancel_restore;
    __pthread_unwind_next;
    __pthread_cleanup_routine;

    # New affinity interfaces.
    pthread_getaffinity_np; pthread_setaffinity_np;
    pthread_attr_getaffinity_np; pthread_attr_setaffinity_np;
  } GLIBC_2.3.2;

  GLIBC_PRIVATE {
    __pthread_initialize_minimal; __pthread_cleanup_upto;
    __pthread_unwind;
  } GLIBC_2.3.3;
