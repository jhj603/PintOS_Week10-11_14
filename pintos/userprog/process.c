#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"

#include "threads/synch.h"

#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* filesys.c에 있는 전역 락 */
extern struct lock filesys_lock;

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
/* 현재 코드는 명령어 전체를 스레드 이름으로 넘김. 이 부분을 수정해 "ls -l"에서 프로그램 이름인 "ls"만 */
/* 파싱해 thread_create의 이름 인자로 넘겨주어야 함. */
tid_t
process_create_initd (const char *file_name) {
	char parse_copy[128];
	char *fn_copy, *program_name, *save_ptr;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	/* "ls -l" 처럼 받은 명령어 문자열을 커널 메모리에 그대로 복사. */
	/* 복사하는 이유는 함수가 종료된 이후 원본 문자열이 사라지거나 변경될 위험(경쟁 상태)을 방지하기 위함 */
	/* 0이 전달되면 플래그를 아무것도 사용하지 않겠다는 것. */
	/* PAL_USER : 유저 메모리에 공간 할당 */
	/* PAL_ZERO : 할당된 페이지의 내용을 0으로 초기화 */
	/* PAL_PANIC : 할당 실패 시, 커널 패닉 */
	fn_copy = palloc_get_page (0);
	/* PAL_PANIC을 사용하지 않았기 때문에 커널 패닉 대신 NULL이 반환되고 그것에 따라 예외 처리 수행 */
	if (fn_copy == NULL)
		return TID_ERROR;
	
	/* 할당받은 페이지에 명령어 문자열 복사 */
	strlcpy (fn_copy, file_name, PGSIZE);
	/* 프로그램 이름 파싱을 위한 명령어 문자열 복사 */
	strlcpy	(parse_copy, file_name, sizeof(parse_copy));

	/* 공백 문자를 기준으로 명령어 문자열 파싱해 프로그램 이름을 얻어냄 */
	program_name = strtok_r(parse_copy, " ", &save_ptr);

	/* Create a new thread to execute FILE_NAME. */
	/* 스레드 생성. file_name("ls"), 스레드 우선순위, 스레드가 실행할 함수 시작 주소, */
	/* start_process에 넘겨줄 복사된 명령어 문자열의 주소 전달 */
	tid = thread_create (program_name, PRI_DEFAULT, initd, fn_copy);

	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);

	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
/* 부모 프로세스의 메모리, 파일 디스크립터 등 대부분의 자원을 복사해 */
/* 자식 프로세스 생성 */
/* 부모와 자식에게 서로 다른 값을 반환해 둘을 구분할 수 있게 함. */
/* 부모 프로세스에게는 새로 생성된 자식 프로세스의 ID(PID) 반환 */
/* 자식 프로세스에게는 0을 반환 */
/* 프로세스 생성 실패 시 -1 반환 */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	struct thread* cur = thread_current();

	/* 1. 실행 컨텍스트 복제 : 부모 프로세스가 시스템 콜을 호출했을 때의 CPU 상태(레지스터 값 등)가 */
	/* 담긴 struct intr_frame을 자식에게 복사해줘야 함. 자식의 반환값(rax 레지스터)은 0으로 설정 */
	cur->parent_if = if_;

	/* 2. 프로세스 생성 : 자식 프로세스로 실행될 새로운 커널 스레드를 thread_create 함수로 생성 */
	tid_t child_tid = thread_create(name, PRI_DEFAULT, __do_fork, cur);

	if (TID_ERROR == child_tid)
	{
		return TID_ERROR;
	}

	/* 3. 부모-자식 관계 동기화 : 커널은 부모-자식 관계를 추적할 수 있어야 함. */
	/* 가장 중요한 부분. 부모는 자식이 모든 자원 복제를 완료할 때까지 대기해야 함. 자식이 리소스 부족으로 복제 실패 시, */
	/* 부모는 에러(-1)를 반환해야 함. 세마포어를 이용해 동기화 구현 */
	struct thread* child = NULL;
	struct list_elem* e;

	for (e = list_begin(&cur->child_list); e != list_end(&cur->child_list); e = list_next(e))
	{
		struct thread* t = list_entry(e, struct thread, child_elem);

		if (child_tid == t->tid)
		{
			child = t;

			/* 자원이 복제될 때까지 대기 */
			sema_down(&child->fork_sema);
			
			break;
		}
	}

	if (NULL == child)
	{
		return TID_ERROR;
	}

	/* 자식 프로세스가 복제에 실패했을 때(예: 메모리 부족) */
	if (-1 == child->exit_status)
	{
		/* 자식이 정상적으로 종료되고 리소스가 해제되도록 wait() 호출 */
		process_wait(child_tid);

		return TID_ERROR;
	}

	return child_tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
/* 메모리 복제 헬퍼 함수 */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	/* 1. 부모 페이지가 커널 페이지면 복사 대상이 아니므로 즉시 반환 */
	if (is_kernel_vaddr(va))
	{
		/* 복제 실패가 아니라 더 이상 복제할 필요가 없으니 성공을 반환하고 건너뜀. */
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	/* 2. 부모의 가상 주소 va에 매핑된 물리 페이지를 찾음. */
	parent_page = pml4_get_page (parent->pml4, va);

	if (NULL == parent_page)
	{
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	/* 3. 메모리 공간 복제(주소 공간) : 자식 프로세스는 부모와 독립된 자신만의 메모리 공간을 가져야 함. */
	newpage = palloc_get_page(PAL_USER);

	if (NULL == newpage)
	{
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	/* 4. 부모의 페이지 테이블(pml4)을 순회하며 매핑된 모든 유저 메모리 페이지에 대해 새로운 물리 페이지를 할당하고 내용을 복사해야 함. */
	memcpy(newpage, parent_page, PGSIZE);
	/* 쓰기 가능 여부 확인 */
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	/* 5. 자식의 페이지 테이블에 새 페이지 매핑 */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		/* 6. 매핑 실패 시, 할당받았던 페이지를 해제하고 실패 반환 */
		palloc_free_page(newpage);

		return false;
	}

	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	/* 1. 부모의 인터럽트 프레임(CPU context)을 복사하고, 자식의 반환값을 0으로 설정 */
	memcpy (&if_, parent->parent_if, sizeof (struct intr_frame));
	/* 자식 프로세스의 fork() 반환 값은 0 */
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
	{
		goto error;
	}

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
	{
		goto error;
	}
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	lock_acquire(&filesys_lock);

	/* 3. 파일 디스크립터 복제 : 부모가 열어둔 파일들을 상속받음. 부모의 파일 디스크립터 테이블을 복사하되, 각 파일에 대해 */
	/* file_duplicate()를 호출해야 함. 이러면 파일 내 현재 위치는 독립적으로 관리 가능 */
	for (int i = 0; i < FDT_COUNT_LIMIT; ++i)
	{
		if (NULL != parent->fd_table[i])
		{
			/* file_duplicate는 같은 inode를 공유하지만 별도의 file 객체 생성 */
			current->fd_table[i] = file_duplicate(parent->fd_table[i]);

			if (NULL == current->fd_table[i])
			{
				for (int j = 0; j < i; ++j)
				{
					if (NULL != current->fd_table[j])
					{
						file_close(current->fd_table[j]);
					}
				}
				/* file_duplicate 실패 시, lock을 해제하고 에러 처리로 넘어감 */
				lock_release(&filesys_lock);
				
				goto error;
			}
		}
	}

	lock_release(&filesys_lock);

	process_init ();

	/* 4. 복제가 성공했음을 부모에게 알림 */
	sema_up(&current->fork_sema);

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
		
error:
	/* 4. 복제 실패를 부모에게 알리고 스레드 종료*/
	current->exit_status = -1;
	sema_up(&current->fork_sema);

	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
/* 새로 만들어진 스레드가 CPU를 할당받아 처음으로 실행하는 함수 */
/* 현재 load는 단순히 파일만 로드. load가 성공한 후, if_.rsp가 가리키는 유저 스택에 명령어 인자들을 직접 채워 넣어줘야 함 */
int
process_exec (void *f_name) {
	/* thread_create에서 넘겨준 명령어 문자열 포인터를 저장. 경쟁 상태 방지 */
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context.
	 * 새로운 프로그램을 로드하기 전에, 현재 프로세스의 주소 공간과 리소스를 정리합니다. */
	process_cleanup ();

	/* And then load the binary */
	/* 디스크에서 메모리로 이진 파일 로드 */
	/* 다음에 실행될 명령어 주소(유저 프로그램의 첫 실행 명령어 주소)와 */
	/* 유저 스택의 최상단 주소를 각각 rip, rsp에 저장*/
	success = load (file_name, &_if);

	/* If load failed, quit. */
	palloc_free_page (file_name);
	/* 로드 실패(파일이 없거나 ELF 형식이 잘못된 경우) 시 스레드 종료 */
	if (!success)
	{
		return -1;
	}

	/* Start switched process. */
	/* 로드 성공 시 유저 모드로 전환할 준비 */
	/* 현재 커널 스택 포인터(rsp)를 load가 채워준 if_(interrupt frame)의 주소로 강제 전환 */
	/* intr_exit 어셈블리 루틴으로 점프. 스택에 저장된 값들을 CPU 레지스터로 복원하고 iret 명령어를 실행 */
	/* 커널 모드를 탈출하고 유저 프로그램의 첫 명령어(if_.rip)부터 실행 시작*/
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	struct thread *cur = thread_current(), *child = NULL;
	struct list_elem* e;

	/* 현재 실행 중인 프로세스의 자식 리스트를 처음부터 끝까지 순회 */
	for (e = list_begin(&cur->child_list); e != list_end(&cur->child_list); e = list_next(e))
	{
		struct thread* t = list_entry(e, struct thread, child_elem);

		/* child_tid와 일치하는 자식 프로세스를 찾으면 */
		if (child_tid == t->tid)
		{
			child = t;

			break;
		}
	}

	/* child_tid를 가진 자식이 없다면 잘못된 요청으로 -1을 반환하고 함수를 종료해야 함. */
	/* 이미 wait()가 호출된 자식이어도 동일하게 -1을 반환해야 함. */
	if (NULL == child || child->is_waited)
	{
		return -1;
	}

	/* wait()가 호출되었음을 표시 */
	child->is_waited = true;

	/* 자식 프로세스가 종료될 때까지 대기 */
	sema_down(&child->wait_sema);

	/* 자식의 종료 상태를 가져오고 자식 리스트에서 제거 */
	int exit_status = child->exit_status;
	list_remove(&child->child_elem);

	/* 자식 프로세스의 리소스 정리 허용 */
	/* 부모가 자식의 정보를 모두 얻었으니, 자식은 이제 완전히 소멸해도 좋다고 알림. */
	sema_up(&child->free_sema);

	/* 자식의 종료 상태 반환 */
	return exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	/* 프로세스 종료 메시지 출력. 모든 종료 경로를 처리할 수 있음. */
	printf("%s: exit(%d)\n", curr->name, curr->exit_status);

	/* 부모가 먼저 종료될 때, 자식 프로세스들이 고아가 되어 영원히 대기하는 것 방지 */
	/* 부모의 자식 리스트 순회해 각 자식이 스스로 종료될 수 있도록 free_sema를 올려줌. */
	struct list_elem* e = list_begin(&curr->child_list);
	while (e != list_end(&curr->child_list))
	{
		struct thread* child = list_entry(e, struct thread, child_elem);

		e = list_next(e);
		/* 부모-자식 관계를 끊음 */
		child->parent = NULL;

		/* 자식이 부모의 wait 호출 없이도 종료될 수 있도록 함 */
		sema_up(&child->free_sema);
	}
	
	/* 프로세스 종료될 때, 열려있는 모든 파일 닫아야 함. */
	lock_acquire(&filesys_lock);

	if (NULL != curr->fd_table)
	{
		for (int i = 2; i < FDT_COUNT_LIMIT; ++i)
		{
			if (NULL != curr->fd_table[i])
			{
				file_close(curr->fd_table[i]);
			}
		}
	}

	/* 실행 중인 파일 닫기 */
	if (NULL != curr->exec_file)
	{
		file_close(curr->exec_file);
	}

	lock_release(&filesys_lock);

	if (NULL != curr->parent)
	{
		/* 자식 프로세스는 process_wait에서 잠들어 있는 부모를 깨우는 동기화 작업을 수행해야 함. */
		/* process_wait에서 sema_down 시킨 wait_sema를 sema_up 해 부모를 깨움. */
		sema_up(&curr->wait_sema);

		/* 부모가 자신의 상태를 모두 읽고 신호를 보내줄 때까지 대기 */
		/* 부모에서 sema_up을 수행시켜줘야 자식은 메모리에서 해제될 수 있음. */
		sema_down(&curr->free_sema);	
	}

	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
/* 디스크에 있는 실행 파일(ELF)을 읽어 프로세스가 실행될 수 있도록 메모리 이미지를 만듬 */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	int len, argc = 0;
	char *copy_command = NULL, *save_ptr = NULL;
	char **parse_args = NULL, **user_args = NULL;

	/* Allocate and activate page directory. */
	/* 프로세스 고유의 페이지 디렉토리를 생성 */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
	{
		return false;
	}
	process_activate (thread_current ());

	/* 1. 명령어 파싱 : load 함수가 받은 file_name을 공백 기준으로 단어들로 분리해야 함. */
	copy_command = palloc_get_page (0);
	/* 명령어 문자열의 최대 길이는 128바이트이고 한 글자와 공백은 2바이트이기 때문에 최대 64개의 매개변수가 존재 */
	parse_args = malloc(sizeof(char*) * 64);
	user_args = malloc(sizeof(char*) * 64);
	/* PAL_PANIC을 사용하지 않았기 때문에 커널 패닉 대신 NULL이 반환되고 그것에 따라 예외 처리 수행 */
	if ((NULL == copy_command) || (NULL == parse_args) || (NULL == user_args))
	{
		goto done;
	}
	
	/* 할당받은 페이지에 명령어 문자열 복사 */
	strlcpy (copy_command, file_name, PGSIZE);

	/* 공백을 기준으로 명령어 문자열을 파싱해 저장 */
	for (char* arg = strtok_r(copy_command, " ", &save_ptr); arg != NULL; arg = strtok_r(NULL, " ", &save_ptr))
	{
		parse_args[argc++] = arg;
	}

	/* 파싱된 명령어 문자열이 없을 때(빈 명령어 문자열일 때) */
	if (!argc)
	{
		goto done;
	}

	lock_acquire(&filesys_lock);

	/* Open executable file. */
	/* 실행 파일 열기. 파싱 결과의 첫 번째가 프로그램 이름 */
	file = filesys_open (parse_args[0]);
	if (file == NULL) {
		printf ("load: %s: open failed\n", parse_args[0]);

		goto release_done;
	}

	/* 실행 중인 파일에 쓰기를 금지. 만약 프로그램 실행 중 디스크에 있는 실행 파일 원본 변경 시 */
	/* 예기치 않은 동작이나 시스템 충돌 유발. 쓰기 작업을 할 수 없도록 보호해야 함. -> rox-로 시작하는 테스트 케이스 통과에 필수 */
	file_deny_write(file);

	/* Read and verify executable header. */
	/* 파일의 첫 부분을 읽어 ELF 매직 넘버가 맞는지 확인해 유효한 실행 파일인지 검증 */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", parse_args[0]);

		goto file_done;
	}

	/* Read program headers. */
	/* ELF 파일 내부에 있는 프로그램 헤더들을 하나씩 읽음. */
	/* "파일의 0x1000번 위치부터 200바이트를 읽어, 가상 메모리 0x8048000에 올려라"와 같은 정보가 저장 */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
		{
			goto file_done;
		}
			
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
		{
			goto file_done;
		}
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto file_done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					/* 각 프로그램 헤더의 정보에 따라 load_segment() 호출 */
					/* palloc_get_page()로 물리 메모리 페이지를 할당받고, pagedir_set_page()를 통해 /*
					/* 프로세스의 가상 주소와 물리 페이지를 매핑한 뒤, 파일에서 해당 부분을 읽어 메모리를 채움 */
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
					{
						goto file_done;
					}
				}
				else
				{
					goto file_done;
				}
				break;
		}
	}
	
	/* Set up stack. */
	/* 유저 스택을 위한 물리 메모리 페이지(보통 1개)를 할당하고, 가상 주소 공간의 꼭대기(PHYS_BASE 바로 아래)에 매핑한 뒤, */
	/* 초기 스택 포인터 값을 설정 */
	if (!setup_stack (if_))
	{
		goto file_done;
	}

	/* Start address. */
	/* 모든 로딩이 성공하면 파일 실행 시작 주소(rip)와 스택 포인터 주소(rsp)를 */
	/* process_exec 함수에 반환 */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	/* 이 부분이 Argument Passing */
	/* load 함수가 인자로 받은 struct intr_frame* if_의 내용을 수정해 process_exec로 전달해야 함. */
	/* 현재 setup_stack은 단순히 비어있는 스택 페이지를 하나 만들고 if_->rsp를 그 꼭대기(USER_STACK)으로 설정할 뿐. */
	
	/* 2. 스택에 인자 저장 : 파싱된 문자열들을 if_->rsp가 가리키는 스택의 위에서부터 아래로 쌓아야 함. */
	for (int i = (argc - 1); i > -1; --i)
	{
		/* 문자열의 길이. 널 문자까지 포함해야 하므로 +1 */
		len = strlen(parse_args[i]) + 1;

		/* 스택 오버플로우 확인 : 인자를 저장할 공간이 스택에 충분한지 확인 */
		/* USER_STACK - PGSIZE = 스택 페이지 시작 주소 */
		if ((uintptr_t)USER_STACK - PGSIZE > (if_->rsp - len))
		{
			goto file_done;
		}
		
		/* rsp를 문자열의 길이만큼 빼서 공간 확보 */
		if_->rsp -= len;
		/* rsp부터 위쪽으로 확보한 공간을 문자열로 복사 */
		memcpy((void*)if_->rsp, parse_args[i], len);
		/* 문자열의 시작 주소가 저장된 유저 스택의 주소를 저장 */
		user_args[i] = (char*)if_->rsp;
	}
	/* 이 때, 워드 정렬을 위해 패딩을 추가해야 함.*/
	while (if_->rsp % 8)
	{
		--if_->rsp;
		/* rsp를 1 바이트만큼 감소시켰기 때문에 1 바이트 크기의 의미 없는 데이터로 채움 */
		*((uint8_t*)if_->rsp) = 0;
	}
	/* 문자열들의 주소(포인터)를 스택에 쌓아야 함. 이것이 argv 배열의 내용이 됨. 배열의 끝을 알리는 NULL부터 넣어야 함. */
	if_->rsp -= 8;
	*((uint64_t*)if_->rsp) = 0;

	for (int i = (argc - 1); i > -1; --i)
	{
		if_->rsp -= 8;
		*((uint64_t*)if_->rsp) = (uint64_t)user_args[i];
	}
	/* argv 배열의 시작 주소, argc의 값, 그리고 가짜 반환 주소(fake return address)를 스택에 쌓아야 함. */

	/* 3. 레지스터 값 업데이트 : */
	/* if_->rsp : 스택에 데이터를 모두 쌓았기 때문에 스택 포인터는 처음 위치보다 아래로 내려와 있음. */
	/* 이 최종 스택 포인터 주소로 if_->rsp 값을 업데이트 해야 함. */
	/* if_->R.rdi : main 함수의 첫 번째 인자인 argc의 값을 저장 */
	if_->R.rdi = argc;
	/* if_->R.rsi : main 함수의 두 번째 인자인 argv의 시작 주소 저장. 현재 rsp의 위치가 argv의 시작 주소 */
	if_->R.rsi = if_->rsp;
	/* if_->rip : 이미 ehdr.e_entry로 프로그램의 시작 주소로 설정되었기 때문에 변경 필요 없음 */

	if_->rsp -= 8;
	/* 가짜 반환 주소 저장 */
	*((uint64_t*)if_->rsp) = 0;	

	/* 이후 load 함수가 true를 반환하고 process_exec 함수가 do_iret(&_if)를 호출해 저장해준 값들을 */
	/* 실제 CPU 레지스터에 로드하고 유저 모드로 전환해 프로그램이 main 함수부터 올바른 인자들과 함께 실행되도록 함. */

	success = true;

	/* 실행 파일을 스레드에 저장 */
	t->exec_file = file;
	/* 성공 시 파일 닫기를 건너뜀 */
	goto release_done;

file_done:
	file_close(file);

release_done:
	lock_release(&filesys_lock);

done:
	/* We arrive here whether the load is successful or not. */
	palloc_free_page(copy_command);

	free(parse_args);
	free(user_args);

	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
