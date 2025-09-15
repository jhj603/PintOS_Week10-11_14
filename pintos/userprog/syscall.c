#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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
	
	thread_exit ();
}
