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
#include "userprog/process.h"

/* 함수 포인터 타입 정의 */
typedef void syscall_handler_func(struct intr_frame* f);

/* filesys.c에 있는 전역 락 */
extern struct lock filesys_lock;

/* 함수 포인터 배열 선언 */
static syscall_handler_func* syscall_handlers[SYS_END];

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* 시스템 콜 */
/* syscall0 */
void sys_halt(struct intr_frame* f);

/* syscall1 */
void sys_exit(struct intr_frame* f);
void sys_open(struct intr_frame* f);
void sys_close(struct intr_frame* f);
void sys_filesize(struct intr_frame* f);
void sys_wait(struct intr_frame* f);
void sys_exec(struct intr_frame* f);
void sys_tell(struct intr_frame* f);
void sys_remove(struct intr_frame* f);

/* syscall2 */
void sys_create(struct intr_frame* f);
void sys_fork(struct intr_frame* f);
void sys_seek(struct intr_frame* f);

/* syscall3 */
void sys_write(struct intr_frame* f);
void sys_read(struct intr_frame* f);

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

	/* Project 2 */
	syscall_handlers[SYS_HALT] = sys_halt;
	syscall_handlers[SYS_EXIT] = sys_exit;
	syscall_handlers[SYS_FORK] = sys_fork;
	syscall_handlers[SYS_EXEC] = sys_exec;
	syscall_handlers[SYS_WAIT] = sys_wait;
	syscall_handlers[SYS_CREATE] = sys_create;
	syscall_handlers[SYS_REMOVE] = sys_remove;
	syscall_handlers[SYS_OPEN] = sys_open;
	syscall_handlers[SYS_FILESIZE] = sys_filesize;
	syscall_handlers[SYS_READ] = sys_read;
	syscall_handlers[SYS_WRITE] = sys_write;
	syscall_handlers[SYS_SEEK] = sys_seek;
	syscall_handlers[SYS_TELL] = sys_tell;
	syscall_handlers[SYS_CLOSE] = sys_close;

	/* Project 3 and optionally Project 4 */
	syscall_handlers[SYS_MMAP] = NULL;
	syscall_handlers[SYS_MUNMAP] = NULL;

	/* Project 4 only */
	syscall_handlers[SYS_CHDIR] = NULL;
	syscall_handlers[SYS_MKDIR] = NULL;
	syscall_handlers[SYS_READDIR] = NULL;
	syscall_handlers[SYS_ISDIR] = NULL;
	syscall_handlers[SYS_INUMBER] = NULL;
	syscall_handlers[SYS_SYMLINK] = NULL;

	/* Extra for Project 2 */
	syscall_handlers[SYS_DUP2] = NULL;
	syscall_handlers[SYS_MOUNT] = NULL;
	syscall_handlers[SYS_UMOUNT] = NULL;
}

/* The main system call interface */
/* 유저 모드에서 커널 모드로 전환되어 시스템 콜을 호출. */
/* rax 레지스터 값을 확인해 어떤 시스템 콜을 호출할 지 파악해야 함. intr_frame 구조체에 들어있음*/
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	/* 1. 스택에서 시스템 콜 번호를 가져옴. */
	uint64_t syscall_num = f->R.rax;

	/* 2. 번호가 유효한 범위인지 확인 */
	if ((SYS_HALT <= syscall_num) && (SYS_END > syscall_num))
	{
		/* 3. 배열에서 해당 번호의 핸들러 함수 포인터를 가져옴 */
		syscall_handler_func* handler = syscall_handlers[syscall_num];

		/* 4. 핸들러가 등록되어 있다면 호출 */
		if (NULL != handler)
		{
			/* 함수 포인터를 통해 실제 함수 호출 */
			handler(f);

			return;
		}
	}

	/* 유효하지 않은 시스템 콜 번호거나 핸들러가 등록되지 않은 경우 */
	/* 프로세스 종료 */
	thread_current()->exit_status = -1;

	thread_exit();
}

/* 유저가 전달한 단일 주소가 유효한지 검사하는 함수. 문자열을 넘길 시 문자열의 시작 주소만 검증 */
void check_address(void* addr)
{
	/* 1. 주소가 NULL은 아닌지 */
	/* 2. 유저 영역 주소인지(커널 영역 침범 방지) */
	/* 3. 할당된 페이지인지(페이지 폴트 방지) */
	if ((NULL == addr) || is_kernel_vaddr(addr) || (NULL == pml4_get_page(thread_current()->pml4, addr)))
	{
		thread_current()->exit_status = -1;
		
		thread_exit();
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
void sys_halt(struct intr_frame* f)
{
	power_off();
}

/* 현재 프로세스 종료 시스템 콜 */
void sys_exit(struct intr_frame* f)
{
	/* 종료 상태가 됐음을 저장 */
	thread_current()->exit_status = (int)f->R.rdi;

	/* 현재 커널 스레드 종료. 한 프로세스가 한 커널 스레드 위에서 동작시키기 때문에 프로세스 종료 */
	thread_exit();
}

/* 파일 이름에 해당하는 파일을 여는 시스템 콜 */
/* 해당 파일을 읽거나 쓸 수 있도록 준비시키는 함수 */
/* 성공적으로 파일을 열면 그 파일을 식별할 수 있는 고유한 정수 값인 */
/* 파일 식별자를 반환해야 함. */
void sys_open(struct intr_frame* f)
{
	const char* file = (const char*)f->R.rdi;
	/* 1. 인자 검증 */
	check_valid_string(file);

	/* 파일 이름이 비어있다면 -1 반환 */
	/* filesys_open에서도 처리가 되지만, 전역 락을 걸고 작업을 하는 비싼 작업을 하기 전에 */
	/* 미리 빠르게 실패를 반환할 수 있어 미미하지만 긍정적인 성능 향상이 있음 .*/
	if ('\0' == *file)
	{
		f->R.rax = -1;
		return;
	}

	struct thread* cur = thread_current();
	/* 2. 파일 시스템 함수 호출을 통해 파일 열기 */
	lock_acquire(&filesys_lock);
	struct file* open_file = filesys_open(file);
	lock_release(&filesys_lock);

	/* 3. 파일이 없거나 열기 실패 시 -1 반환 */
	if (NULL == open_file)
	{
		f->R.rax = -1;
		return;
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
		lock_acquire(&filesys_lock);
		file_close(open_file);
		lock_release(&filesys_lock);

		f->R.rax = -1;
		return;
	}
	
	cur->fd_table[fd] = open_file;

	f->R.rax = fd;
}

/* open으로 열었던 파일을 닫아 해당 파일과 연결된 시스템 자원을 해제하는 시스템 콜 */
/* 1. 자원 해제 : 파일 객체가 사용하던 메모리 해제 */
/* 2. 파일 디스크립터 반환 : 프로세스의 파일 디스크립터 테이블에서 해당 항목을 비움. */
/* 그 번호를 다른 파일을 열 때 재사용할 수 있도록 함. */
/* 3. 데이터 동기화 : 파일의 아이노드(inode)를 닫아 파일이 완전히 닫혔음을 시스템에 알림. */
void sys_close(struct intr_frame* f)
{
	int fd = f->R.rdi;

	struct thread* cur = thread_current();

	/* 1. 인자(fd) 검증 : 파일 디스크립터가 유효한지 확인 */
	/* fd가 유효한 범위 내에 있는지 (2 이상 FDT_COUNT_LIMIT 미만) */
	/* 해당 fd가 실제 열려 있는 파일을 가리키고 있는지 (파일 디스크립터 테이블의 해당 슬롯이 NULL이 아닌지) */
	if ((2 > fd) || (FDT_COUNT_LIMIT <= fd) || (NULL == cur->fd_table[fd]))
	{
		cur->exit_status = -1;

		thread_exit();
	}

	/* 2. 파일 닫기 : 파일 디스크립터 테이블에서 파일 객체 포인터를 가져와 file_close() 호출 */
	lock_acquire(&filesys_lock);
	file_close(cur->fd_table[fd]);
	lock_release(&filesys_lock);

	/* 3. 테이블 정리 : 파일 디스크립터 테이블의 해당 슬롯을 NULL로 설정해 fd가 비어있음을 표시 */
	cur->fd_table[fd] = NULL;
}

/* 열려 있는 파일의 크기를 바이트 단위로 알려주는 시스템 콜 */
void sys_filesize(struct intr_frame* f)
{
	int fd = f->R.rdi;

	struct thread* cur = thread_current();

	/* 1. fd 유효성 검사 */
	if ((2 > fd) || (FDT_COUNT_LIMIT <= fd) || (NULL == cur->fd_table[fd]))
	{
		f->R.rax = -1;
		return;
	}

	/* 2. 파일 크기 반환 - file_length() 함수 호출 */
	lock_acquire(&filesys_lock);
	int size = file_length(cur->fd_table[fd]);
	lock_release(&filesys_lock);

	f->R.rax = size;
}

/* 자식 프로세스가 종료될 때까지 기다리고, 자식의 종료 상태를 반환하는 시스템 콜 */
void sys_wait(struct intr_frame* f)
{
	f->R.rax = process_wait((int)f->R.rdi);
}

/* 현재 실행 중인 프로세스를 새로운 프로그램으로 교체하는 시스템 콜 */
/* 새로 만드는 fork와 달리 현재 프로세스의 메모리 이미지(코드, 데이터, 스택)을 지우고 */
/* 지정된 실행 파일을 그 자리에 로드해 실행. PID는 바뀌지 않으며, 성공하면 원래 프로그램으로 */
/* 다시 돌아오지 않음. */
void sys_exec(struct intr_frame* f)
{
	const char* file = (const char*)f->R.rdi;

	/* 1. 인자 검증 : 파일 이름 문자열이 유효한 주소인지 확인 */
	check_valid_string(file);

	/* 2. process_exec 호출을 위해 f_name 복사본 생성 */
	char* fn_copy = palloc_get_page(0);

	if (NULL == fn_copy)
	{
		/* exec 시스템 콜은 실패 시 -1을 반환하고 호출한 프로세스는 계속 실행되어야 함. */
		f->R.rax = -1;
		return;
	}

	strlcpy(fn_copy, file, PGSIZE);

	/* 3. process_exec 호출. 성공 시 반환하지 않고 실패 시 -1 반환 */
	if (-1 == process_exec(fn_copy))
	{	
		/* 종료 상태가 됐음을 저장 */
		thread_current()->exit_status = -1;

		/* 현재 커널 스레드 종료. 한 프로세스가 한 커널 스레드 위에서 동작시키기 때문에 프로세스 종료 */
		thread_exit();
	}
}

/* 열려 있는 파일에서 현재 위치를 알려주는 시스템 콜 */
/* 위치 저장 : 현재 위치를 임시로 저장했다가 돌아올 때 사용 */
/* 파일 크기 확인 : seek으로 파일 끝까지 이동한 다음 tell을 호출하면 파일 전체 크기를 알 수 있음. */
/* filesize 시스템 콜이 더 직접적인 방법 */
/* 디버깅 : 파일 I/O작업이 예상대로 진행되고 있는지 현재 위치를 확인하며 디버깅할 때 유용 */
void sys_tell(struct intr_frame* f)
{
	int fd = f->R.rdi;

	struct thread* cur = thread_current();

	/* 1. fd 유효성 검사 */
	if ((2 > fd) || (FDT_COUNT_LIMIT <= fd) || (NULL == cur->fd_table[fd]))
	{
		/* tell은 값을 반환해야 하므로 실패 시 -1 반환 */
		f->R.rax = -1;
		return;
	}

	struct file* target_file = cur->fd_table[fd];

	/* 2. 파일 위치 반환(file_tell 호출) */
	lock_acquire(&filesys_lock);
	unsigned int position = file_tell(target_file);
	lock_release(&filesys_lock);

	f->R.rax = position;
}

/* 파일 또는 디렉토리를 삭제하는 시스템 콜 */
/* 실패하는 경우 */
/* 1. 삭제할 파일이나 디렉토리가 존재하지 않을 때 */
/* 2. 디렉토리가 비어있지 않을 때 */
/* 3. 루트 디렉토리를 삭제하려고 할 때 */
/* 4. 현재 작업 디렉토리나 열려 있는 파일을 삭제하려고 할 때 */
void sys_remove(struct intr_frame* f)
{
	const char* file = (const char*)f->R.rdi;

	/* 1. file 유효성 검사 */
	check_valid_string(file);

	/* 파일/디렉토리 삭제(filesys_remove 호출) */
	lock_acquire(&filesys_lock);
	bool success = filesys_remove(file);
	lock_release(&filesys_lock);

	f->R.rax = success;
}

/* 파일 이름과 초기 크기를 받아 새로운 파일을 생성하는 시스템 콜 */
void sys_create(struct intr_frame* f)
{
	const char* file = (const char*)f->R.rdi;
	unsigned int initial_size = f->R.rsi;

	/* 1. file이 유효 주소인지 검사. 문자열 전체가 유효한 메모리 공간에 있는지 확인해야 함. */
	check_valid_string(file);

	/* 파일 이름이 비어있다면 -1 반환 */
	/* filesys_create에서도 처리가 되지만, 전역 락을 걸고 작업을 하는 비싼 작업을 하기 전에 */
	/* 미리 빠르게 실패를 반환할 수 있어 미미하지만 긍정적인 성능 향상이 있음 .*/
	if ('\0' == *file)
	{
		f->R.rax = 0;
		return;
	}

	/* 2. 파일 시스템 함수 호출 후 결과 반환 */
	/* filesys_create()를 호출해 파일 생성 */
	lock_acquire(&filesys_lock);
	bool success = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	
	f->R.rax = success;
}

/* 현재 실행 중인 프로세스(부모 프로세스)를 거의 그대로 복제해 */
/* 새로운 프로세스(자식 프로세스)를 만드는 시스템 콜 */
void sys_fork(struct intr_frame* f)
{
	const char* thread_name = (const char*)f->R.rdi;

	/* thread_name 유효성 검사 */
	check_valid_string(thread_name);

	f->R.rax = process_fork(thread_name, f);
}

/* 열려 있는 파일 내에서 다음 데이터를 읽거나 쓸 위치를 변경하는 시스템 콜 */
/* 파일의 특정 부분에만 접근하는 임의 접근을 가능하게 하는 시스템 콜 */
/* fd : 위치를 변경할 파일의 식별자 */
/* position : 파일의 시작 지점으로부터 몇 바이트 떨어진 곳으로 이동할지를 나타내는 새로운 위치 */
void sys_seek(struct intr_frame* f)
{
	int fd = f->R.rdi;
	unsigned int pos = f->R.rsi;

	struct thread* cur = thread_current();

	/* 1. fd 유효성 검사 */
	if ((2 > fd) || (FDT_COUNT_LIMIT <= fd) || (NULL == cur->fd_table[fd]))
	{
		/* 유효하지 않은 fd라면 프로세스 종료 */
		cur->exit_status = -1;

		/* 현재 커널 스레드 종료. 한 프로세스가 한 커널 스레드 위에서 동작시키기 때문에 프로세스 종료 */
		thread_exit();

		return;
	}

	struct file* target_file = cur->fd_table[fd];

	/* 2. 파일 위치 이동(file_seek 호출) */
	lock_acquire(&filesys_lock);
	file_seek(target_file, pos);
	lock_release(&filesys_lock);
}

/* 열려 있는 파일이나 표준 출력(콘솔)에 데이터를 쓰는 시스템 콜 */
/* 1. fd : 데이터를 쓸 대상을 가리키는 파일 식별자 */
/* 2. buffer : 파일이나 콘솔에 쓸 데이터가 담겨 있는 버퍼 주소 */
void sys_write(struct intr_frame* f)
{
	int fd = f->R.rdi;
	const void* buffer = (const void*)f->R.rsi;
	unsigned int size = f->R.rdx;

	/* 작성할 데이터 크기가 0이라면 0 반환 후 함수 종료 */
	if (0 == size)
	{
		f->R.rax = 0;
		return;
	}

	/* buffer가 유효 주소인지 검사 */
	check_valid_buffer(buffer, size);

	int bytes_write = -1;

	/* 표준 출력 식별자라면 */
	if (STDOUT_FILENO == fd)
	{
		/* 버퍼의 내용을 콘솔에 출력하는 커널 레벨 함수 */
		/* 내부적으로 콘솔 락을 사용하기 때문에 버퍼 내용 모두 출력 보장 */
		putbuf(buffer, size);
		
		bytes_write = size;
	}
	/* 일반 파일 처리(fd > 1) */
	else if ((STDOUT_FILENO < fd) && (FDT_COUNT_LIMIT > fd))
	{
		struct file* open_file = thread_current()->fd_table[fd];

		/* 열린 적 없는 파일이라면 -1 반환 */
		if (NULL == open_file)
		{
			f->R.rax = -1;
			return;
		}

		lock_acquire(&filesys_lock);
		bytes_write = file_write(open_file, buffer, size);
		lock_release(&filesys_lock);
	}
	
	/* 그 외 파일 식별자는 유효하지 않으므로 -1 반환 */
	f->R.rax = bytes_write;
}

/* 열려 있는 파일이나 장치로부터 데이터를 읽어와 */
/* 메모리의 특정 공간(buffer)에 저장하는 시스템 콜 */
/* 1. fd : 데이터를 읽어올 파일을 가리키는 파일 식별자 */
/* 2. buffer : 읽어온 데이터를 저장할 메모리 공간 */
void sys_read(struct intr_frame* f)
{
	int fd = f->R.rdi;
	void* buffer = (void*)f->R.rsi;
	unsigned int size = f->R.rdx;

	/* 읽어올 데이터 크기가 0이라면 0 반환 후 함수 종료 */
	if (0 == size)
	{
		f->R.rax = 0;
		return;
	}

	/* 1. 인자 유효성 검사 */
	check_valid_buffer(buffer, size);

	int bytes_read = -1;

	/* 2. 표준 입력(키보드)으로부터 읽기 */
	if (STDIN_FILENO == fd)
	{
		char* local_buf = (char*)buffer;

		/* size와 타입을 맞춰주기 위함. */
		for (unsigned int i = 0; i < size; ++i)
		{
			local_buf[i] = input_getc();
		}

		bytes_read = size;
	}
	/* 3. 일반 파일으로부터 읽기 */
	else if ((STDOUT_FILENO < fd) && (FDT_COUNT_LIMIT > fd))
	{
		struct thread* cur = thread_current();

		if (NULL == cur->fd_table[fd])
		{
			f->R.rax = -1;
			return;
		}

		lock_acquire(&filesys_lock);
		bytes_read = file_read(cur->fd_table[fd], buffer, size);
		lock_release(&filesys_lock);
	}

	/* 그 외의 fd는 유효하지 않으므로 -1 반환 */
	f->R.rax = bytes_read;
}