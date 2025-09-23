#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef USERPROG
#include "synch.h"
#endif
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* 파일 디스크립터 테이블 크기 */
#define FDT_COUNT_LIMIT 128

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
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */

	/* 부모 프로세스 */
	struct thread* parent;
	/* 자식 프로세스의 정보를 담을 리스트 */
	struct list child_list;
	/* 부모 프로세스의 리스트에 자신을 저장하기 위한 변수 */
	struct list_elem child_elem;
	
	/* 자식 프로세스가 종료될 때까지 부모가 기다리기 위한 세마포어 */
	struct semaphore wait_sema;
	/* 부모가 자식의 종료 상태를 수신할 때까지 자식의 소멸을 대기시키기 위한 세마포어 */
	struct semaphore free_sema;
	/* 프로세스 종료 상태 값 */
	int exit_status;
	/* wait() 중복 호출 방지 플래그 */
	bool is_waited;

	/* 파일 식별자를 저장할 테이블 */
	/* 프로세스당 최소 2개에서 최대 64개의 파일을 저장할 수 있어야 함. */
	/* 대부분의 PintOS 프로젝트 명세에서는 2개에서 64개까지를 요구하기 때문에 넉넉하게 128 사용 */
	/* 각 프로세스가 자신만의 파일 디스크립터 테이블을 가짐 */
	/* sys_open으로 생성된 새로운 file 객체의 포인터를 현재 프로세스의 fd_table의 비어있는 슬롯에 저장 */
	/* sys_read, sys_write, sys_close 등은 인자로 받은 fd를 사용해 현재 프로세스의 fd_table에서 올바른 file 객체를 */
	/* 찾아 작업을 수행하기 때문에 다른 프로세스에 영향을 주지 않음. */
	struct file* fd_table[FDT_COUNT_LIMIT];

	/* fork 동기화를 위한 세마포어 */
	struct semaphore fork_sema;
	/* 부모의 유저 스택 정보를 자식에게 전달하기 위한 포인터 */
	struct intr_frame* parent_if;

	/* 프로세스가 종료될 때 파일을 닫기 위한 실행 파일 포인터 */
	struct file* exec_file;
#endif	
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
