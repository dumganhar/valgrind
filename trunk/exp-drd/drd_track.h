/*
  This file is part of drd, a data race detector.

  Copyright (C) 2006-2008 Bart Van Assche
  bart.vanassche@gmail.com

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
  02111-1307, USA.

  The GNU General Public License is contained in the file COPYING.
*/


void drd_post_thread_join(DrdThreadId joiner, DrdThreadId joinee);

void drd_pre_mutex_init(Addr mutex, SizeT size, const MutexT mutex_type);
void drd_post_mutex_destroy(Addr mutex, const MutexT mutex_type);
void drd_pre_mutex_lock(DrdThreadId tid, Addr mutex, const SizeT size,
                        const MutexT mutex_type);
void drd_post_mutex_lock(DrdThreadId tid, Addr mutex, const SizeT size,
                         const MutexT mutex_type);
void drd_pre_mutex_unlock(const DrdThreadId tid, const Addr mutex,
                          const MutexT mutex_type);

void drd_post_cond_init(Addr cond, SizeT s);
void drd_pre_cond_destroy(Addr cond);

void drd_semaphore_init(const Addr semaphore, const SizeT size,
                        const Word pshared, const Word value);
void drd_semaphore_destroy(const Addr semaphore);
void drd_semaphore_pre_wait(const DrdThreadId tid, const Addr semaphore,
                            const SizeT size);
void drd_semaphore_post_wait(const DrdThreadId tid, const Addr semaphore,
                             const Bool waited);
void drd_semaphore_pre_post(const DrdThreadId tid, const Addr semaphore,
                            const SizeT size);
void drd_semaphore_post_post(const DrdThreadId tid, const Addr semaphore,
                             const SizeT size, const Bool waited);

void drd_barrier_init(const Addr barrier, const SizeT size, const Word count);
void drd_barrier_destroy(const Addr barrier);
void drd_barrier_pre_wait(const DrdThreadId tid, const Addr barrier);
void drd_barrier_post_wait(const DrdThreadId tid, const Addr barrier,
                           const Bool waited);
