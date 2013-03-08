/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, int initial_count)
{
        struct semaphore *sem;

        KASSERT(initial_count >= 0);

        sem = kmalloc(sizeof(struct semaphore));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void 
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 * Bridge to the wchan lock, so if someone else comes
		 * along in V right this instant the wakeup can't go
		 * through on the wchan until we've finished going to
		 * sleep. Note that wchan_sleep unlocks the wchan.
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_lock(sem->sem_wchan);
		spinlock_release(&sem->sem_lock);
                wchan_sleep(sem->sem_wchan);

		spinlock_acquire(&sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(struct lock));
        if (lock == NULL) {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }

        // add stuff here as needed

        lock->lk_wchan = wchan_create("Lock wchan");
        if (lock->lk_wchan == NULL) {
          kfree (lock->lk_name);
          kfree (lock);
          return NULL;
        }

        spinlock_init(&lock->lk_spinlock);
        lock->lk_curthread = NULL;
        lock->lk_hold = 0;

        //end

        return lock;
}

void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);

        // add stuff here as needed

        if (lock->lk_hold == 0) {
          spinlock_cleanup(&lock->lk_spinlock);
          wchan_destroy(lock->lk_wchan);
        }
        else
          panic("Lock destory called when being held. Bad code, bad");
        //end
        kfree(lock->lk_name);
        kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
        // Write this

        spinlock_acquire(&lock->lk_spinlock);

        while(lock->lk_hold == 1) {

          wchan_lock(lock->lk_wchan);
          spinlock_release(&lock->lk_spinlock);
          wchan_sleep(lock->lk_wchan);
          spinlock_acquire(&lock->lk_spinlock);
        }

        lock->lk_hold = 1;
        lock->lk_curthread = curthread;

        spinlock_release(&lock->lk_spinlock);

        //end

        //(void)lock;  // suppress warning until code gets written
}

void
lock_release(struct lock *lock)
{
        // Write this

        KASSERT(lock != NULL);

        //spinlock_acquire(&lock->lk_spinlock);

        if (lock_do_i_hold(lock)) {

          spinlock_acquire(&lock->lk_spinlock);
          lock->lk_curthread = NULL;
          lock->lk_hold = 0;
          // Two days of struggle and me not
          // reading the code right was the
          // problem!!! wchan will not wake
          // someone up randomly. This is there
          // in the wchan include file.
          wchan_wakeone(lock->lk_wchan);
          spinlock_release(&lock->lk_spinlock);
        }

        //end

        //(void)lock;  // suppress warning until code gets written
}

bool
lock_do_i_hold(struct lock *lock)
{
        // Write this

        KASSERT(curthread->t_in_interrupt == false);

        if (curthread == lock->lk_curthread)
          return true;
        else
          return false;

        //end

        //(void)lock;  // suppress warning until code gets written

        //return true; // dummy until code gets written
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{

  struct cv *cv;

  cv = kmalloc(sizeof(struct cv));
  if (cv == NULL) {
    return NULL;
  }

  cv->cv_name = kstrdup(name);
  if (cv->cv_name==NULL) {
    kfree(cv);
    return NULL;
  }

  // add stuff here as needed

  cv->cv_wchan = wchan_create("CV wchan");
  if (cv->cv_wchan == NULL) {

    kfree(cv->cv_name);
    kfree(cv);
    return NULL;
  }

  spinlock_init(&cv->cv_spinlock);

  // end add stuff

  return cv;
}

void
cv_destroy(struct cv *cv)
{

  KASSERT(cv != NULL);

  // add stuff here as needed

  spinlock_cleanup(&cv->cv_spinlock);
  wchan_destroy(cv->cv_wchan);

  // end add stuff

  kfree(cv->cv_name);
  kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{

  // Write this

  int spinlock_flag = 0;

  KASSERT(curthread->t_in_interrupt == false);

  spinlock_acquire(&cv->cv_spinlock);

  if (lock_do_i_hold(lock)) {

    lock_release(lock);

    wchan_lock(cv->cv_wchan);
    spinlock_flag = 1;
    spinlock_release(&cv->cv_spinlock);

    wchan_sleep(cv->cv_wchan);
    lock_acquire(lock);

  }

  if (!spinlock_flag)
    spinlock_release(&cv->cv_spinlock);

  // end write this

  // (void)cv;    // suppress warning until code gets written
  // (void)lock;  // suppress warning until code gets written
}

void
cv_signal(struct cv *cv, struct lock *lock)
{

  // Write this

  int spinlock_flag = 0;

  KASSERT(curthread->t_in_interrupt == false);

  spinlock_acquire(&cv->cv_spinlock);

  if (lock_do_i_hold(lock)) {

    spinlock_acquire(&cv->cv_spinlock);
    wchan_wakeone(cv->cv_wchan);
    spinlock_flag = 1;
    spinlock_release(&cv->cv_spinlock);
  }

  if (!spinlock_flag)
    spinlock_release(&cv->cv_spinlock);

  // end write this

  //(void)cv;    // suppress warning until code gets written
  //(void)lock;  // suppress warning until code gets written
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{

  // Write this

  int spinlock_flag = 0;

  KASSERT(curthread->t_in_interrupt == false);

  spinlock_acquire(&cv->cv_spinlock);

  if (lock_do_i_hold(lock)) {

    wchan_wakeall(cv->cv_wchan);

    spinlock_flag = 1;

    spinlock_release(&cv->cv_spinlock);
  }

  if (!spinlock_flag)
    spinlock_release(&cv->cv_spinlock);

  // end write this

  //(void)cv;    // suppress warning until code gets written
  //(void)lock;  // suppress warning until code gets written
}


/////////////////////////////////////////////////////
// RW Locks
//

struct rwlock * rwlock_create(const char *name) {

  struct rwlock *rwlock;

  rwlock = kmalloc(sizeof(struct rwlock));
  if (rwlock == NULL) return NULL;

  rwlock->rwl_name = kstrdup(name);
  if (rwlock->rwl_name == NULL) {
    kfree(lock);
    return NULL;
  }

  rwlock->rwl_rwchan = wchan_create("RWLock Reader Wait Channel");
  rwlock->rwl_wwchan = wchan_create("RWLock Writer Wait Channel");
  if (rwlock->rwl_rwchan == NULL || rwlock->rwl_rwchan == NULL) {
    kfree (rwlock->rwl_name);
    kfree (rwlock);
    return NULL;
  }

  spinlock_init(&rwlock->rwl_spinlock);

  mode = -1;

  sem_create(rwlock->rwl_rsem);
  sem_create(rwlock->rwl_wsem);
  if (rwlock->rwl_rsem == NULL || rwlock->rwl_wsem == NULL) {
    kfree (rwlock->rwl_name);
    kfree (rwlock);
  }
  return rwlock;
}

void rwlock_destory(struct rwlock *rwlock) {

  KASSERT(rwlock != NULL);

  spinlock_cleanup (&rwlock->rwl_spinlock);
  wchan_destroy (rwlock->rwl_rwchan);
  wchan_destroy (rwlock->rwl_wwchan);
  sem_destory (rwlock->rsem);
  sem_destory (rwlock->wsem);
  kfree (rwlock->rwl_name);
  kfree (rwlock);
}

void rwlock_acquire_read(struct rwlock *rwlock) {

  KASSERT(rwlock != NULL)

  spinlock_acquire (&rwlock->rwl_spinlock);

  while (1) {

    // Reader has the lock. So NP!
    if (mode == 1 || mode = -1) {
      V(rwlock->rwl_rsem);
      mode = 1;
      break;
    }
    // Writer has a lock. We sleep and wait.
    else if (mode == 0) {
      wchan_lock(rwlock->rwl_rwchan);
      spinlock_release(&rwlock->rwl_spinlock);
      wchan_sleep(rwlock->rwl_rwchan);
      spinlock_acquire(&rwlock->rwl_spinlock);
    }
  }

  spinlock_release (&rwlock->rwl_spinlock);
}

void rwlock_read_release(struct rwlock * rwlock) {

  KASSERT(rwlock != NULL);

  spinlock_acquire(&rwlock->rwl_spinlock);

  // Reader has the lock. Simply decrease count.
  if (mode == 1 && ratio > 0.5) {
    P(rwlock->rwl_rsem);
    if (rwlock->rwl_rsem->sem_count == 0)
      rwlock->mode = -1;
    ratio = rwlock->rwl_rsem->count / rwlock->rwl_wsem->count;
    spinlock_release(&rwlock->rwl_spinlock);
    return;
  }

  // If the rwlock is not acquired at all
  // acquire it and incereasr the count.
  if (mode == -1) {
    mode = 1;
    V(rw->rwl_rsem);
    spinlock_release(&rwlock->rwl_spinlock);
    return;
  }

  // Writer has the lock. Do stuff to prevent
  // starvation.
  while (mode == 0 && ratio <= 0.5) {

    spinlock_release(&rwl->rwl_spinlock);
    wchan_sleep(rwlock->rwl_wchan);
    spinlock_acquire(&rwl->rwl_spinlock);
  }

  return;

  spinlock_release(&rwlock->rwl_spinlock);
}

void rwlock_acquire_write(struct rwlock *rwlock) {

  (void) rwlock;

  return;
}
