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
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
void argument_stack(char **argv, int argc, struct intr_frame *if_);

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
/* 📌 initd 실행 */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char *ptr;
    strtok_r(file_name, " ", &ptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
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
	/* 📌 드디어.. process_exec 실행 */
	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
    struct thread *curr = thread_current();
    if (if_ == NULL)
        return TID_ERROR;

    memcpy(&curr->parent_if, if_, sizeof(struct intr_frame));

    tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork, curr);
    if (tid == TID_ERROR)
        return TID_ERROR;

    struct thread *child = get_child_process(tid);
    if (child == NULL) {
        printf("process_fork: get_child_process returned NULL tid=%d parent=%s(%d)\n",
               tid, curr->name, curr->tid);
        return TID_ERROR;
    }

    /* 부모는 자식이 초기화해서 sema_up 해줄 때까지 한 번만 대기합니다. */
    sema_down(&child->fork_sema);

    if (child->exit_status == TID_ERROR) {
        /* 자식이 초기화에 실패했음 */
        printf("process_fork: child init failed tid=%d\n", tid);
        return TID_ERROR;
    }

    return tid;
}


#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va))
        return true;

	/* 2. Resolve VA from the parent's page map level 4. */
    parent_page = pml4_get_page(parent->pml4, va);
    if (parent_page == NULL)
        return false;

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	 newpage = palloc_get_page(PAL_ZERO);
    if (newpage == NULL)
        return false;

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
    writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void __do_fork(void *aux) {
    struct intr_frame if_;
    struct thread *parent = (struct thread *) aux;
    struct thread *current = thread_current();
    bool succ = true;

    /* (1) 부모 intr_frame 복사 */
    memcpy(&if_, &parent->parent_if, sizeof(struct intr_frame));
    if_.R.rax = 0;
	

    /* (2) create new pml4 for child */
    current->pml4 = pml4_create();
    if (current->pml4 == NULL)
        goto error;
    process_activate(current);

#ifndef VM
    if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
        goto error;
#else
    /* VM일 경우 supplemental_page_table 복사 로직 사용 */
    supplemental_page_table_init(&current->spt);
    if (!supplemental_page_table_copy(&current->spt, &parent->spt))
        goto error;
#endif

    /* (3) fd table 복제 */
    current->next_fd = parent->next_fd;
    for (int fd = FD_MIN; fd < FD_MAX; fd++) {
        if (parent->fd_table[fd] == NULL) {
            current->fd_table[fd] = NULL;
        } else {
            current->fd_table[fd] = file_duplicate(parent->fd_table[fd]);
            if (current->fd_table[fd] == NULL)
                goto error;
        }
    }

    /* (4) 부모를 깨워서 fork() 호출자(부모)가 계속 진행하게 함 */
    sema_up(&current->fork_sema);

    process_init();
	
    /* (5) 자식 프로세스로 전환 */
    do_iret(&if_);
    NOT_REACHED();

error:
    sema_up(&current->fork_sema); /* 항상 부모를 깨움 (실패든 성공이든) */
    current->exit_status = TID_ERROR;
    thread_exit();
}


/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */

void argument_stack(char **argv, int argc, struct intr_frame *if_){
	char *arg_addr[128];
	int args_len = 0;
	for (int i = argc - 1; i >= 0; i--) {
    int args_len = strlen(argv[i]) + 1;
    if_->rsp -= args_len;
    memcpy((void *)if_->rsp, argv[i], args_len);   // ← 캐스팅
    arg_addr[i] = (char *)if_->rsp;                
	}

	while (if_->rsp % 8) *(uint8_t *)(--if_->rsp) = 0;

	if_->rsp -= 8;
	memset((void *)if_->rsp, 0, sizeof(char *));       // ← 캐스팅

	for (int i = argc - 1; i >= 0; i--) {
		if_->rsp -= 8;
		memcpy((void *)if_->rsp, &arg_addr[i], sizeof(char *));  // ← 캐스팅
	}

	if_->rsp -= 8;
	memset((void *)if_->rsp, 0, sizeof(void *));       // ← 캐스팅

	if_->R.rdi = argc;
	if_->R.rsi = if_->rsp + 8;



}

struct thread 
*get_child_process(int pid) 
{
    struct thread *curr = thread_current();
    struct thread *t;

    for (struct list_elem *e = list_begin(&curr->child_list); e != list_end(&curr->child_list); e = list_next(e)) {
        t = list_entry(e, struct thread, child_elem);

        if (pid == t->tid)
            return t;
    }

    return NULL;
}


// int
// process_exec (void *f_name) {
// 	char *file_name = f_name;
// 	char *full = f_name; 
// 	bool success;
	
// 	/* We cannot use the intr_frame in the thread structure.
// 	 * This is because when current thread rescheduled,
// 	 * it stores the execution information to the member. */
	
// 	/*인터럽트 발생 시 CPU 레지스터의 상태를 저장하는 구조체*/
// 	/* 📌 예를 들어서, do_iret같은 함수 호출 시 push하는 인자들, 함수가 끝난 후 돌아갈 주소, 스택 포인터 등의 정보를 담는다. */
// 	struct intr_frame _if;
// 	/*ds,es,ss,cs : 이 레지스터들은 세그먼트 셀렉터를 설정*/
// 	/*SEL_UDSEG : 사용자 데이터 세그먼트, SEL_UCSEG : 사용자 코드 세그먼트*/
// 	/* 📌 세그먼트 레지스터를 설정한다.
// 	 * 세그먼트 레지스터는 CPU가 메모리 영역을 구분하기 위해 사용하는 레지스터이다. 
// 	 * 일종의 이름표를 새겨 넣는 작업 : " 이 이름표가 붙은 메모리 영역은 유저프로그램이 데이터를 읽고쓰는데 사용하는 곳이다" */
// 	_if.ds = _if.es = _if.ss = SEL_UDSEG;
// 	_if.cs = SEL_UCSEG;
// 	/*eflags 레지스터는 CPU의 상태 플래그를 담고 있다.*/
// 	_if.eflags = FLAG_IF | FLAG_MBS;

// 	/* We first kill the current context */
// 	/* 현재 실행 중인 프로세스의 자원들을 해제하고 프로세스 컨텍스트를 제거하며 이는 새로운 프로
// 	세스가 이전 프로세스의 흔적 없이 깨끗한 상태에서 시작할 수 있도록 보장*/
// 	process_cleanup ();

// 	char *saveptr = NULL;
//     char *argv[32];
//     int argc = 0;

//     // for (char *tok = strtok_r(full, " ", &saveptr);
//     //     tok != NULL && argc < 32;
//     //     tok = strtok_r(NULL, " ", &saveptr)) {
//     //     argv[argc++] = tok;
//     // }

// 	// for (char *arg = strtok_r(full, " ", &saveptr); arg != NULL; arg = strtok_r(NULL, " ", &saveptr))
//     //     argv[argc++] = arg;
	
// 	for (char *tok = strtok_r(full, " ", &saveptr);tok != NULL && argc < 32;tok = strtok_r(NULL, " ", &saveptr)) {
//   		argv[argc++] = tok;
// 	}

//     if (argc == 0) {
//         palloc_free_page(full);
//         return -1;
//     }
//     char *prog = argv[0];
    
//     success = load(prog, &_if);
// 	argument_stack(argv, argc, &_if);
	
// 	/* And then load the binary */
// 	/*file_name으로 지정된 실행 파일을 메모리에 로드하며 로딩 과정에는 다음이 포함된다.
// 	1. 실행 파일의 형식을 파싱한다(예:ELF 형식)
// 	2. 코드와 데이터를 메모리로 읽어들인다.
// 	3. 스택을 설정하고 _if의 멤버에 새로운 프로세스의 시작 주소와 스택 포인터를 설정*/

// 	/* If load failed, quit. */
// 	palloc_free_page (file_name);
// 	if (!success)
// 		return -1;

// 	// hex_dump(_if.rsp,_if.rsp,USER_STACK-_if.rsp,true);
// 	/* iret은 스택에서 레지스터(cs, eip, eflags,esp,ss)값을 복원하여 새로운 사용자 모드 코드로 점프*/
// 	/* Start switched process. */
// 	do_iret (&_if);
// 	NOT_REACHED ();
// }

int
process_exec (void *f_name) {
    char *file_name = f_name;
    bool success;

    /* 스레드 구조에서는 intr_frame을 사용할 수 없습니다.
     * 현재 쓰레드가 재스케줄 되면 실행 정보를 멤버에게 저장하기 때문입니다. */
    struct intr_frame if_;
    if_.ds = if_.es = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;

    /* We first kill the current context */
    process_cleanup();

    /** #Project 2: Command Line Parsing - 문자열 분리 */
    char *ptr, *arg;
    int argc = 0;
    char *argv[64];

    for (arg = strtok_r(file_name, " ", &ptr); arg != NULL; arg = strtok_r(NULL, " ", &ptr))
        argv[argc++] = arg;

    /* And then load the binary */
    success = load(file_name, &if_);

    /* If load failed, quit. */
    if (!success)
        return -1;

    argument_stack(argv, argc, &if_);

    palloc_free_page(file_name);

    /** #Project 2: Command Line Parsing - 디버깅용 툴 */
    // hex_dump(if_.rsp, if_.rsp, USER_STACK - if_.rsp, true);

    /* Start switched process. */
    do_iret(&if_);
    NOT_REACHED();
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
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

    struct thread *child = get_child_process(child_tid);
    if (child == NULL)
        return -1;

    sema_down(&child->wait_sema);  // 자식 프로세스가 종료될 때 까지 대기.

    int exit_status = child->exit_status;
    list_remove(&child->child_elem);

    sema_up(&child->exit_sema);  // 자식 프로세스가 죽을 수 있도록 signal

    return exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
    struct thread *curr = thread_current();

    /* 부모가 process_wait()에서 기다리고 있을 수 있으니 먼저 wake up 시킨다. */
    sema_up(&curr->wait_sema);

    /* 부모가 exit_status를 읽고 sema_up(&child->exit_sema) 해줄 때까지 대기 */
    sema_down(&curr->exit_sema);

    /* 이제 안전하게 프로세스 자원 정리 */
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
/* 실행할 프로그램의 binary 파일을 메모리에 올리는 역할 */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	
	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());
	
	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
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
				goto done;
			/* 파일이 제대로 된 ELF인지 검사하는 과정이 동반되며, 세그먼트 단위로 하나씩 메모리로 올리는 작업을 진행 */
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
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}
	/* 스택 만들기 !! */
	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
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
