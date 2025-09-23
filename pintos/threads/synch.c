#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"


/*
 * 세마포어 구조체 정의
 * struct semaphore { unsigned value; struct list waiters; }
 * value : 사용 가능한 리소스 개수
 * waiters : 대기 중인 스레드들의 우선순위 리스트
*/  

// [세마포어를 초기화 하는 함수]
void
sema_init (struct semaphore *sema, unsigned value) {
   // 세마포어가 널 값이면 커널 패닉을 띄우고 함수를 즉시 종료 시킨다. 
	ASSERT (sema != NULL);
   // 세마포어 카운트를 사용 가능한 리소스 개수로 초기화 한다.
	sema->value = value;
   // [대기중인 스레드들의 우선순위 리스트를 초기화 하는 함수]
   // 빈 리스트로 초기화 한다.
	list_init (&sema->waiters);
}

// [리스트를 정렬하는 비교하기 위한 함수]
// 첫번째로 들어오는 객체의 우선순위와 두번째로 들어오는 객체의 우선순위 비교 결과를 bool 값으로 반환
bool
cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
   struct thread *thread_a = list_entry(a, struct thread, elem);
   struct thread *thread_b = list_entry(b, struct thread, elem);
   return thread_a->priority > thread_b->priority;
}


// [P연산, 자원을 획득하거나 자원이 없으면 스레드를 block 시키는 함수]
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;
   // 세마포어가 널 값이면 즉시 종료
	ASSERT (sema != NULL);
   // 현재 실행 흐름이 스레드 문맥이 아니라, 하드웨어 인터럽트를 처리하는 문맥이라면 즉시 종료
	ASSERT (!intr_context ());

   // 세마포어 대기열을 원자적으로 조작하기 위해서 잠시 인터럽트를 종료
	old_level = intr_disable ();

	// [자원이 없으면 대기를 시키는 함수]
	while (sema->value == 0) {
		// [우선순위를 고려하여 현재 스레드를 대기열에 삽입하는 함수]
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);
      // [현재 스레드를 Block 시키고 스케줄러에게 CPU 양보하는 함수]
		thread_block ();
	}

   /*   
    * 현재 대기열에는 여러 개의 스레드가 자원이 없어서 block이 되어 있는 상태
    * 즉 각각의 스레드 들이 본인의 sema_down 속 while에 갇혀 있는 상태
    * 그러다가 sema_up에 의해서 가장 우선순위가 높은 스레드가 깨어나고,
    * 깨어난 스레드가 자신의 sema_down을 마저 실행하면서 sema->value-- 에 의해서 자원을 획득 하는 것
   */
	sema->value--;
   
   // 대기열의 조작 즉, 누가 자원을 쓸 지 정해졌으니 잠시 꺼두었던 인터럽트를 다시 실행
	intr_set_level (old_level);
}


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

// [V연산, 자원을 반납하거나 자원을 사용할 스레드를 깨우는 함수]
void 
sema_up(struct semaphore *sema) {
   enum intr_level old_level;
   struct thread *to_unblock = NULL;
   bool need_yield = false;

   // 세마포어가 널 값이면 즉시 종료
   ASSERT (sema != NULL);
   // 대기열 조작을 위해 잠시 인터럽트 종료
   old_level = intr_disable();

   // 대기열에 기다리고 있는 스레드들이 있다면
   if (!list_empty(&sema->waiters)) {
      
      // 혹시라도 순서가 흐트러졌을까봐 한번 더 정렬 확인
      list_sort(&sema->waiters, cmp_priority, NULL);
      // 우선순위가 가장 높은 스레드를 꺼내서 Ready 큐에 넣어서 스케줄링 후보로 등록
      to_unblock = list_entry(list_pop_front(&sema->waiters), struct thread, elem);
      thread_unblock(to_unblock);

      // 방금 깬 애가 현재 실행중인 스레드보다 우선순위가 더 높다면 양보 필요하다고 표시
      if (to_unblock->priority > thread_current()->priority) need_yield = true;
   }

   // 자원을 반납
   sema->value++;

   // 인터럽트 문맥이면 당장이 아닌 핸들러 리턴 직후 양보하도록 예약
   if (intr_context() && need_yield) intr_yield_on_return();
   // 인터럽트 상태를 복원
   intr_set_level(old_level);

   // 일반 문맥이면, 크리티컬 섹션을 벗어난 뒤 양보하도록 예약
   if (!intr_context() && need_yield)
      thread_yield();
}



static void sema_test_helper (void *sema_);

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


/*
 * 뮤텍스 락 구조체 정의
 * struct lock { struct thread *holder; struct semaphore semaphore; };
 * holder : 현재 이 락을 소유한 스레드를 가리키는 포인터
 * semaphore : 락을 구현하기 위한 내부 세마포어
*/ 

// [뮤텍스 락 초기화 함수]
void
lock_init (struct lock *lock) {
   // 락이 널 값이면 즉시 종료
	ASSERT (lock != NULL);
   // 소유하고 있는 스레드 초기화
	lock->holder = NULL;
   // 첫 스레드는 무조건 바로 자원 사용 가능하니까 1로 설정해서 바로 sema_down 할 수 있도록 초기화
	sema_init (&lock->semaphore, 1);
}

// [기부 리스트의 우선순위 정렬을 위한 비교 함수]
static bool donation_cmp(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct thread *ta = list_entry(a, struct thread, donation_elem);
    struct thread *tb = list_entry(b, struct thread, donation_elem);
    return ta->priority > tb->priority;
}

// 우선순위 기부의 전파 깊이를 제한하는 상수값 정의
#define DONATION_MAX_DEPTH 8


// [락을 획득하고 우선순위를 기부하는 함수]
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