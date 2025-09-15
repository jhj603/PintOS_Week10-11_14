#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/palloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* 시스템 콜 */
int sys_write(int fd, const void* buffer, unsigned int size);
void sys_exit(int status);

/* 유효 주소 검사 헬퍼 함수 */
void check_address(void* addr);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
/* 유저 모드에서 커널 모드로 전환되어 시스템 콜을 호출. */
/* rax 레지스터 값을 확인해 어떤 시스템 콜을 호출할 지 파악해야 함. intr_frame 구조체에 들어있음*/
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	printf ("system call!\n");

	/* 요청 식별 : f->R.rax 값을 확인해 어떤 요청인지 식별해야 함. */
	switch (f->R.rax)
	{
		/* Project 2 */
		case SYS_HALT:
		break;
		case SYS_EXIT:
		sys_exit(f->R.rdi);
		break;
		case SYS_FORK:
		break;
		case SYS_EXEC:
		break;
		case SYS_WAIT:
		break;
		case SYS_CREATE:
		break;
		case SYS_REMOVE:
		break;
		case SYS_OPEN:
		break;
		case SYS_FILESIZE:
		break;
		case SYS_READ:
		break;
		case SYS_WRITE:
		f->R.rax = sys_write(f->R.rdi, (const void*)f->R.rsi, f->R.rdx);
		break;
		case SYS_SEEK:
		break;
		case SYS_TELL:
		break;
		case SYS_CLOSE:
		break;

		/* Project 3 and optionally Project 4 */
		case SYS_MMAP:
		break;
		case SYS_MUNMAP:
		break;

		/* Project 4 only */
		case SYS_CHDIR:
		break;
		case SYS_MKDIR:
		break;
		case SYS_READDIR:
		break;
		case SYS_ISDIR:
		break;
		case SYS_INUMBER:
		break;
		case SYS_SYMLINK:
		break;

		/* Extra for Project 2 */
		case SYS_DUP2:
		break;
		case SYS_MOUNT:
		break;
		case SYS_UMOUNT:
		break;
	}
	
	/* switch문이 끝난 후 프로세스를 종료시키는 것이기 때문에 제거해야 함. */
	//thread_exit ();
}

/* 유저가 전달한 주소가 유효한지 검사하는 함수. */
void check_address(void* addr)
{
	/* 1. 주소가 NULL은 아닌지 */
	/* 2. 유저 영역 주소인지(커널 영역 침범 방지) */
	/* 3. 할당된 페이지인지(페이지 폴트 방지) */
	if ((NULL == addr) || is_kernel_vaddr(addr) || (NULL == pml4_get_page(thread_current()->pml4, addr)))
	{
		sys_exit(-1);
	}
}

/* 파일에 데이터를 쓰는 시스템 콜 */
int sys_write(int fd, const void* buffer, unsigned int size)
{
	/* buffer가 유효 주소인지 검사 */
	check_address(buffer);

	/* 표준 출력 식별자라면 */
	if (STDOUT_FILENO == fd)
	{
		/* 버퍼의 내용을 콘솔에 출력하는 커널 레벨 함수 */
		/* 내부적으로 콘솔 락을 사용하기 때문에 버퍼 내용 모두 출력 보장 */
		putbuf(buffer, size);
		
		return size;
	}

	/* 파일에 쓰는 경우는 나중에. */

	return -1;
}

/* 현재 프로세스 종료 시스템 콜 */
void sys_exit(int status)
{
	printf("%s: exit(%d)\n", thread_current()->name, status);
	/* 현재 커널 스레드 종료. 한 프로세스가 한 커널 스레드 위에서 동작시키기 때문에 프로세스 종료 */
	thread_exit();
}