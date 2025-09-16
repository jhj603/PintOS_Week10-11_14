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

#include "threads/init.h"
#include "filesys/filesys.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* 시스템 콜 */
/* syscall0 */
void sys_halt(void);

/* syscall1 */
void sys_exit(int status);

/* syscall2 */
int sys_create(const char* file, unsigned int initial_size);

/* syscall3 */
int sys_write(int fd, const void* buffer, unsigned int size);


/* 유효 주소 검사 헬퍼 함수 */
void check_address(void* addr);
void check_valid_buffer(const void* buffer, unsigned int size);
void check_valid_string(const char* str);

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
	//printf ("system call!\n");

	/* 요청 식별 : f->R.rax 값을 확인해 어떤 요청인지 식별해야 함. */
	switch (f->R.rax)
	{
		/* Project 2 */
		case SYS_HALT:
		sys_halt();
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
		f->R.rax = sys_create((const char*)f->R.rdi, f->R.rsi);
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

/* 유저가 전달한 단일 주소가 유효한지 검사하는 함수. 문자열을 넘길 시 문자열의 시작 주소만 검증 */
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

/* 유저가 전달한 버퍼가 유효한지 검사하는 함수(페이지 단위) */
/* 주소의 크기를 알 때 사용. 포인터와 함께 명시적인 크기 정보가 주어질 때 사용 */
/* 버퍼 단위 검사. 버퍼가 차지하는 메모리 공간의 시작 페이지부터 끝 페이지까지만 순회해 */
/* 각 페이지의 유효성 검사. */
/* 10KB 크기의 버퍼가 3개 페이지에 걸쳐 있다면, 모든 바이트(10240개)를 검사하기보다 */
/* 단 3개의 페이지만 검사하면 됨. */
void check_valid_buffer(const void* buffer, unsigned int size)
{
	/* 버퍼의 시작부터 끝까지 페이지 단위로 순회하며 유효성 검사 */
	for (void* p = pg_round_down(buffer); p < buffer + size; p += PGSIZE)
	{
		check_address(p);
	} 
}

/* 유저가 전달한 문자열이 유효한지 검사하는 함수(페이지 단위) */
/* 문자열 시작 주소를 먼저 검사. 문자열을 한 글자씩 순회하다가 */
/* 페이지 경계를 넘어가는 시점에만 다음 페이지의 유효성 검사 */
void check_valid_string(const char* str)
{
	/* 문자열의 시작 주소부터 NULL 문자를 만날 때까지 검사 */
	check_address(str);

	while ('\0' != *str)
	{
		/* 페이지 경계를 넘어갈 때만 다음 페이지의 유효성 검사 */
		if (pg_ofs(str) == (PGSIZE - 1))
		{
			check_address(str + 1);
		}

		++str;
	}
}

/* 시스템 작동을 완전히 중단시키고 정지시키는 시스템 콜 */
void sys_halt(void)
{
	power_off();
}

/* 현재 프로세스 종료 시스템 콜 */
void sys_exit(int status)
{
	struct thread* cur = thread_current();
	
	/* 종료 상태가 됐음을 저장 */
	cur->exit_status = status;

	printf("%s: exit(%d)\n", cur->name, status);

	/* 현재 커널 스레드 종료. 한 프로세스가 한 커널 스레드 위에서 동작시키기 때문에 프로세스 종료 */
	thread_exit();
}

/* 파일 이름과 초기 크기를 받아 새로운 파일을 생성하는 시스템 콜 */
int sys_create(const char* file, unsigned int initial_size)
{
	/* 1. file이 유효 주소인지 검사. 문자열 전체가 유효한 메모리 공간에 있는지 확인해야 함. */
	check_valid_string(file);

	/* 2. 파일 시스템 함수 호출 후 결과 반환 */
	/* filesys_create()를 호출해 파일 생성 */
	return filesys_create(file, initial_size);
}

/* 파일에 데이터를 쓰는 시스템 콜 */
int sys_write(int fd, const void* buffer, unsigned int size)
{
	/* buffer가 유효 주소인지 검사 */
	check_valid_buffer(buffer, size);

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