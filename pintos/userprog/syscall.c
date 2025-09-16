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
#include "filesys/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* 시스템 콜 */
/* syscall0 */
void sys_halt(void);

/* syscall1 */
void sys_exit(int status);
int sys_open(const char* file);
void sys_close(int fd);

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
		f->R.rax = sys_open((const char*)f->R.rdi);
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
		sys_close(f->R.rdi);
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

/* 파일 이름에 해당하는 파일을 여는 시스템 콜 */
/* 해당 파일을 읽거나 쓸 수 있도록 준비시키는 함수 */
/* 성공적으로 파일을 열면 그 파일을 식별할 수 있는 고유한 정수 값인 */
/* 파일 식별자를 반환해야 함. */
int sys_open(const char* file)
{
	/* 1. 인자 검증 */
	check_valid_string(file);

	/* 파일 이름이 비어있다면 -1 반환 */
	/* filesys_open에서도 처리가 되지만, 전역 락을 걸고 작업을 하는 비싼 작업을 하기 전에 */
	/* 미리 빠르게 실패를 반환할 수 있어 미미하지만 긍정적인 성능 향상이 있음 .*/
	if ('\0' == *file)
	{
		return -1;
	}

	struct thread* cur = thread_current();
	/* 2. 파일 시스템 함수 호출을 통해 파일 열기 */
	struct file* open_file = filesys_open(file);

	/* 3. 파일이 없거나 열기 실패 시 -1 반환 */
	if (NULL == open_file)
	{
		return -1;
	}

	/* 4. 열기 성공 시, 파일 식별자 할당 후 반환 */
	/* 0, 1은 표준 입출력이므로 2부터 사용 가능 */
	/* 2부터 시작해 비어있는 fd 테이블 슬롯 찾기 */
	int fd = 2;
	while ((FDT_COUNT_LIMIT > fd) && (NULL != cur->fd_table[fd]))
	{
		++fd;
	}

	/* 테이블 전부 사용 시 파일 닫고 실패 반환 */
	if (FDT_COUNT_LIMIT <= fd)
	{
		file_close(open_file);
		return -1;
	}
	
	cur->fd_table[fd] = open_file;

	return fd;
}

/* open으로 열었던 파일을 닫아 해당 파일과 연결된 시스템 자원을 해제하는 시스템 콜 */
/* 1. 자원 해제 : 파일 객체가 사용하던 메모리 해제 */
/* 2. 파일 디스크립터 반환 : 프로세스의 파일 디스크립터 테이블에서 해당 항목을 비움. */
/* 그 번호를 다른 파일을 열 때 재사용할 수 있도록 함. */
/* 3. 데이터 동기화 : 파일의 아이노드(inode)를 닫아 파일이 완전히 닫혔음을 시스템에 알림. */
void sys_close(int fd)
{
	struct thread* cur = thread_current();

	/* 1. 인자(fd) 검증 : 파일 디스크립터가 유효한지 확인 */
	/* fd가 유효한 범위 내에 있는지 (2 이상 FDT_COUNT_LIMIT 미만) */
	/* 해당 fd가 실제 열려 있는 파일을 가리키고 있는지 (파일 디스크립터 테이블의 해당 슬롯이 NULL이 아닌지) */
	if ((2 > fd) || (FDT_COUNT_LIMIT <= fd) || (NULL == cur->fd_table[fd]))
	{
		sys_exit(-1);
	}

	/* 2. 파일 닫기 : 파일 디스크립터 테이블에서 파일 객체 포인터를 가져와 file_close() 호출 */
	file_close(cur->fd_table[fd]);

	/* 3. 테이블 정리 : 파일 디스크립터 테이블의 해당 슬롯을 NULL로 설정해 fd가 비어있음을 표시 */
	cur->fd_table[fd] = NULL;
}

/* 파일 이름과 초기 크기를 받아 새로운 파일을 생성하는 시스템 콜 */
int sys_create(const char* file, unsigned int initial_size)
{
	/* 1. file이 유효 주소인지 검사. 문자열 전체가 유효한 메모리 공간에 있는지 확인해야 함. */
	check_valid_string(file);

	/* 파일 이름이 비어있다면 -1 반환 */
	/* filesys_create에서도 처리가 되지만, 전역 락을 걸고 작업을 하는 비싼 작업을 하기 전에 */
	/* 미리 빠르게 실패를 반환할 수 있어 미미하지만 긍정적인 성능 향상이 있음 .*/
	if ('\0' == *file)
	{
		return 0;
	}

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