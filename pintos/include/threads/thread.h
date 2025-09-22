#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h" 
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status
{
	THREAD_RUNNING, /* Running thread. */
	THREAD_READY,	/* Not running but ready to run. */
	THREAD_BLOCKED, /* Waiting for an event to trigger. */
	THREAD_DYING	/* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
typedef tid_t pid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0	   /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63	   /* Highest priority. */

#ifdef USERPROG
struct file;  /* forward declaration */
#endif


/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
  /* ── Identity & scheduling (owned by thread.c) ─────────────────── */
  tid_t tid;                    /* Thread identifier. */
  enum thread_status status;    /* Thread state. */
  int priority;                 /* Priority. */
  char name[16];                /* Name (for debugging). */
  int64_t wake_tick;            /* For alarm sleep. */

  /* --- 추가 (thread.h) --- */
  /* donation 관련 필드들 */
  int init_priority;               /* 기본(원래) 우선순위 */
  struct list donations;           /* 나에게 기부한(대기중인) 스레드들 */
  struct list_elem donation_elem;  /* 다른 스레드의 donations list에서 쓰일 엘렘 */
  struct lock *wait_on_lock;       /* 현재 내가 기다리고 있는 lock (없으면 NULL) */

  /* 우선순리 갱신용 함수 (synch.c, thread.c에서 호출) */
  


  /* ── List links (shared with thread.c / synch.c) ────────────────── */
  struct list_elem elem;        /* Ready queue or semaphore wait list. */
  struct list_elem sleep_elem;  /* Timer sleep list. */

  /* ── User program (present only when USERPROG) ──────────────────── */
#ifdef USERPROG
  uint64_t *pml4;               /* Page-map level 4 (process page table). */
  int exit_status;              /* Process exit code. */

  /* ── File descriptor table (per-process) ───────────────────────── */
  enum { FD_MIN = 2, FD_MAX = 128 };  /* 0/1은 stdin/stdout 예약 */
  struct file *fd_table[FD_MAX];      /* 비어 있으면 NULL */
  int next_fd;                        /* 다음 탐색 시작 위치 */

  struct intr_frame parent_if;  // 부모 프로세스 if
  struct list child_list;
  struct list_elem child_elem;
  struct thread *parent;

	struct semaphore fork_sema;  // fork가 완료될 때 signal
  struct semaphore exit_sema;  // 자식 프로세스 종료 signal
  struct semaphore wait_sema;  // exit_sema를 기다릴 때 사용

#endif

  /* ── Virtual memory (present only when VM) ──────────────────────── */
#ifdef VM
  struct supplemental_page_table spt;
#endif

  /* ── Context switch & guard (owned by thread.c) ─────────────────── */
  struct intr_frame tf;         /* Saved context for switching. */
  unsigned magic;               /* Detects stack overflow. */
};


/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_refresh_priority(struct thread *t);

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame *tf);

#endif /* threads/thread.h */
