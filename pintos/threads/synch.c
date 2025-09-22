/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */


bool
cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct thread *thread_a = list_entry(a, struct thread, elem);
    struct thread *thread_b = list_entry(b, struct thread, elem);
    return thread_a->priority > thread_b->priority;
}


void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	//사용할 수 있는 자원이 없을때만 즉, 대기를 시키기 위한 코드
	while (sema->value == 0) {
		//list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);
		thread_block ();
	}

	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */

//자원을 다 사용하고 난 뒤에 반납할때 호출
void sema_up(struct semaphore *sema) {
  enum intr_level old_level;
  struct thread *to_unblock = NULL;
  bool need_yield = false;

  ASSERT (sema != NULL);
  old_level = intr_disable();

  if (!list_empty(&sema->waiters)) {
    // 대기열이 정렬 삽입이라면 sort는 생략 가능. 안전하게 유지해도 OK.
    list_sort(&sema->waiters, cmp_priority, NULL);

    to_unblock = list_entry(list_pop_front(&sema->waiters),
                            struct thread, elem);
    thread_unblock(to_unblock);

    // 지금 깬 애가 더 높다면 나중에 양보 필요
    if (to_unblock->priority > thread_current()->priority)
      need_yield = true;
  }

  sema->value++;

  // 인터럽트 문맥이면 리턴 직후 양보 예약
  if (intr_context() && need_yield) intr_yield_on_return();

  intr_set_level(old_level);

  // 일반 문맥이면, 크리티컬 섹션을 벗어난 뒤 양보
  if (!intr_context() && need_yield)
    thread_yield();
}



static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* donation list comparator (donation_elem 사용) */
static bool donation_cmp(const struct list_elem *a,
                         const struct list_elem *b,
                         void *aux UNUSED) {
    struct thread *ta = list_entry(a, struct thread, donation_elem);
    struct thread *tb = list_entry(b, struct thread, donation_elem);
    return ta->priority > tb->priority;
}

/* Maximum nested donation depth to avoid pathological cycles */
#define DONATION_MAX_DEPTH 8

void
lock_acquire (struct lock *lock) {
    ASSERT (lock != NULL);
    ASSERT (!intr_context ());
    ASSERT (!lock_held_by_current_thread (lock));

    struct thread *cur = thread_current();

    /* 1) set what we're waiting on (so others can inspect it). */
    cur->wait_on_lock = lock;

    /* 2) Try immediate donation: if lock has a holder, donate our priority.
       We donate only to the immediate holder's donations list (to track
       who donated to that holder). We also propagate priority value up
       the chain (without inserting the donor into upper holders' donation lists). */
    struct thread *holder = lock->holder;
    int depth = 0;
    if (holder != NULL) {
        /* Only insert this donor into the immediate holder's donations list. */
        if (cur->priority > holder->priority) {
            /* elevate holder priority */
            holder->priority = cur->priority;
        }
        /* add donor to holder's donation list (if not yet present).
           A thread waits on only one lock at a time, so donation_elem
           can be safely used. */
        list_insert_ordered(&holder->donations, &cur->donation_elem, donation_cmp, NULL);
    }

    /* Propagate priority value up the chain (no list insert for upper holders). */
    holder = lock->holder;
    while (holder != NULL && depth < DONATION_MAX_DEPTH) {
        /* if my priority higher than holder, bump it to me */
        if (holder->priority < cur->priority)
            holder->priority = cur->priority;
        /* move up the chain */
        if (holder->wait_on_lock != NULL)
            holder = holder->wait_on_lock->holder;
        else
            holder = NULL;
        depth++;
    }

    /* 3) Actually wait on the lock's semaphore (this may block). */
    sema_down(&lock->semaphore);

    /* 4) Got the lock */
    lock->holder = cur;
    cur->wait_on_lock = NULL;
}


void
lock_release (struct lock *lock) {
    ASSERT (lock != NULL);
    ASSERT (lock_held_by_current_thread (lock));

    struct thread *cur = thread_current();

    /* Remove donations that were given to 'cur' specifically for this lock.
       A donor waiting on this lock will have donor->wait_on_lock == lock. */
    struct list_elem *e = list_begin(&cur->donations);
    while (e != list_end(&cur->donations)) {
        struct list_elem *next = list_next(e);
        struct thread *donor = list_entry(e, struct thread, donation_elem);
        if (donor->wait_on_lock == lock) {
            list_remove(e);  /* remove donor from our donations */
            /* donor->donation_elem is now unlinked; donor still exists */
        }
        e = next;
    }

    /* Recompute current thread's effective priority based on remaining donations */
    thread_refresh_priority(cur);

    /* release ownership and wake up waiters */
    lock->holder = NULL;
    sema_up(&lock->semaphore);
}


/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* cond->waiters 정렬에 사용: 각 waiter(내부 세마포어)가 깨울 '최고 우선순위 스레드'를 비교 */
static bool
sema_elem_more_priority (const struct list_elem *a,
                         const struct list_elem *b, void *aux UNUSED) {
  const struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
  const struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);

  /* 각 waiter의 내부 세마 waiters 리스트는 sema_down()에서 이미 우선순위 내림차순 유지 중 */
  const struct list *wa = &sa->semaphore.waiters;
  const struct list *wb = &sb->semaphore.waiters;

  /* 비어있을 일은 거의 없지만(곧 sema_down으로 잠들 상태), 방어적으로 처리 */
  if (list_empty (wa)) return false;
  if (list_empty (wb)) return true;

  const struct thread *ta = list_entry (list_front (wa), struct thread, elem);
  const struct thread *tb = list_entry (list_front (wb), struct thread, elem);

  return ta->priority > tb->priority;  /* 내림차순: 높은 우선순위가 먼저 */
}


/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);

  /* cond->waiters를 '해당 waiter가 깨울 최고 우선순위 쓰레드' 기준으로 정렬 삽입 */
  list_insert_ordered (&cond->waiters, &waiter.elem,
                       sema_elem_more_priority, NULL);

  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}


/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) {
    /* 변화 가능성(도네이션 등)에 대비해 가장 최신 우선순위로 정렬 */
    list_sort (&cond->waiters, sema_elem_more_priority, NULL);

    struct semaphore_elem *se =
      list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem);
    sema_up (&se->semaphore);
  }
}


/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}