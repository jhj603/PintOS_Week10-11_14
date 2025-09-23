#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include "threads/init.h"     // power_off
#include "devices/input.h"    // input_getc
#include "lib/kernel/console.h" // putbuf
#include <string.h>           // strlen, memcpy
#include "threads/vaddr.h"    // pg_round_down 등 쓸 때


struct lock filesys_lock; 

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void sys_halt(void);
void sys_exit (int status); 
pid_t sys_fork(const char *thread_name); 
int sys_exec(const char *cmd_line);
int sys_wait(pid_t pid);
bool sys_create(const char *file, unsigned initial_size);
bool sys_remove (const char *file);
int sys_open (const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, const void *buffer, unsigned size);
void sys_seek (int fd, unsigned position);
unsigned sys_tell (int fd);
void sys_close (int fd); 


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
	 * mode stack. The
	 * refore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);		

}


/*<---------------------------------------------------------------------------------------->*/

// int process_add_file(struct file *f) {
//   if (f == NULL) return -1;

//   struct thread *t = thread_current();
//   int start = t->next_fd;                // 시작 지점 기억
//   int fd = start;

//   do {
//     if (fd >= FD_MAX) fd = FD_MIN;       // 원형 순회
//     if (t->fd_table[fd] == NULL) {
//       t->fd_table[fd] = f;
//       t->next_fd = fd + 1;               // 다음 탐색 시작 위치 업데이트
//       if (t->next_fd >= FD_MAX) t->next_fd = FD_MIN;
//       return fd;
//     }
//     fd++;
//   } while (fd != start);

//   return -1; // 꽉 참
// }

// struct file *process_get_file(int fd) {
//   if (fd < 0 || fd >= FD_MAX) return NULL;
//   return thread_current()->fd_table[fd];
// }

// void process_close_file(int fd) {
//   if (fd < 0 || fd >= FD_MAX) return;
//   struct thread *t = thread_current();
//   struct file *f = t->fd_table[fd];
//   if (f) {
//     t->fd_table[fd] = NULL;
//     file_close(f);
//     // 선택: 더 작은 fd를 빨리 재사용하고 싶다면 next_fd 갱신
//     if (fd < t->next_fd) t->next_fd = fd;
//   }
// }

// /* 프로세스 종료 시 모두 닫기(권장) */
// void process_close_all_files(void) {
//   struct thread *t = thread_current();
//   for (int fd = FD_MIN; fd < FD_MAX; fd++) {
//     if (t->fd_table[fd]) {
//       file_close(t->fd_table[fd]);
//       t->fd_table[fd] = NULL;
//     }
//   }
//   t->next_fd = FD_MIN;
// }


/*<---------------------------------------------------------------------------------------->*/
/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	int sys_number = f->R.rax;

	// Argument 순서
    // %rdi %rsi %rdx %r10 %r8 %r9

	switch (sys_number)
	{
	case SYS_HALT:
		sys_halt();
		break;
	case SYS_EXIT:
		sys_exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = process_fork((const char *) f->R.rdi, f);
		break;
	case SYS_EXEC:
        f->R.rax = sys_exec((const char *)f->R.rdi);
        break;
	case SYS_WAIT:
		f->R.rax = sys_wait(f->R.rdi);
		break;		
	case SYS_CREATE:{
		const char *file = (const char *)f->R.rdi;
        unsigned size = (unsigned)f->R.rsi;
        f->R.rax = sys_create(file, size);
        break;
	}
	case SYS_REMOVE:
		f->R.rax = sys_remove((const char *)f->R.rdi); 
		break;
	case SYS_OPEN:
		f->R.rax = sys_open((const char *) f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = sys_filesize(f->R.rdi);
		break;
	case SYS_READ:
        f->R.rax = sys_read((int)f->R.rdi, (void *)f->R.rsi, (unsigned)f->R.rdx);
        break;
	case SYS_WRITE:
         f->R.rax = sys_write((int)f->R.rdi, (const void *)f->R.rsi, (unsigned)f->R.rdx);
        break;
    case SYS_SEEK:
        sys_seek(f->R.rdi, f->R.rsi);
        break;
    case SYS_TELL:
        f->R.rax = sys_tell(f->R.rdi);
        break;
    case SYS_CLOSE:
        sys_close(f->R.rdi);
        break;
    default:
        sys_exit(-1);
}
}

void check_address(const void *addr)
{
    if (addr == NULL || is_kernel_vaddr(addr) ||
        pml4_get_page(thread_current()->pml4, addr) == NULL)
    {
        /* 잘못된 포인터는 즉시 프로세스 종료 */
		thread_current()->exit_status = -1;
        printf("%s: exit(%d)\n", thread_current()->name, -1);
        thread_exit();
    }
}


static void
check_address_range (void *uaddr, unsigned size) {
    if (size == 0) return;

    uint8_t *start = (uint8_t *)uaddr;
    uint8_t *end   = start + size - 1;

    if (uaddr == NULL || !is_user_vaddr(start) || !is_user_vaddr(end)) {
        sys_exit(-1);
    }

    for (uint8_t *p = (uint8_t *)pg_round_down(start);
        p <= (uint8_t *)pg_round_down(end);
        p += PGSIZE) {
        if (pml4_get_page(thread_current()->pml4, p) == NULL) {
            sys_exit(-1);
        }
        
    }
}


void 
sys_halt(void) 
{
    power_off();
}

void 
sys_exit(int status) 
{
	struct thread *t = thread_current();  
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, t->exit_status);
    thread_exit();
}

int sys_wait(pid_t tid) {
	return process_wait(tid);
}

int 
sys_exec(const char *cmd_line) 
{
	check_address(cmd_line);

	off_t size = strlen(cmd_line) + 1;
	char *cmd_copy = palloc_get_page(PAL_ZERO);

	if (cmd_copy == NULL)
		return -1;

	memcpy(cmd_copy, cmd_line, size);

	if (process_exec(cmd_copy) == -1)
		return -1;

	return 0;  // process_exec 성공시 리턴 값 없음 (do_iret)
}

bool 
sys_create(const char *file, unsigned initial_size){
    
	check_address(file);
    return filesys_create(file, initial_size);
}


bool 
sys_remove(const char *file) 
{
    check_address(file);
    return filesys_remove(file);
}


int 
sys_open(const char *file) 
{
    check_address(file);                 // 유저 포인터 1차 검증

    lock_acquire(&filesys_lock);
    struct file *f = filesys_open(file); // 파일 열기
    lock_release(&filesys_lock);

    if (f == NULL) 
        return -1;

    struct thread *t = thread_current();
    int start = t->next_fd;              // 다음 탐색 시작 위치
    int fd = start;

    /* FD 테이블에서 비어있는 가장 작은 슬롯을 원형 탐색 */
    do {
        if (fd >= FD_MAX) fd = FD_MIN;   // 0/1은 예약 → 2부터
        if (t->fd_table[fd] == NULL) {
            t->fd_table[fd] = f;
            t->next_fd = fd + 1;         // 다음 시작점 업데이트
            if (t->next_fd >= FD_MAX) t->next_fd = FD_MIN;
            return fd;
        }
        fd++;
    } while (fd != start);

    /* 꽉 찼으면 실패 처리 */
    file_close(f);
    return -1;
}




int
sys_filesize (int fd){
	if (fd==1) return -1; 
	if(fd < 0 || fd >= FD_MAX) return -1;

	struct thread *t = thread_current();
	struct file *fp = t->fd_table[fd]; 

	if (fp == NULL) return -1;  

	return file_length(fp);
}

int
sys_read (int fd, void *buffer, unsigned size){
	
	if (fd==1) return -1; 
	if(fd < 0 || fd >= FD_MAX) return -1;
	check_address_range(buffer, size);
	if (fd == 0) {
        
        unsigned i = 0;
        unsigned char *buf = (unsigned char *)buffer;
        for (; i < size; i++) {
            buf[i] = input_getc();
    	}
        return (int)i;
    }

	struct thread *t = thread_current();
	struct file *fp = t->fd_table[fd];
	if (fp == NULL) return -1;

	lock_acquire(&filesys_lock);
	int n = file_read(fp,buffer,size); 
	lock_release(&filesys_lock);

	return n;
}



int 
sys_write(int fd, const void *buffer, unsigned size) {
    if (size == 0) return 0;
    check_address_range(buffer, size); 

    if (fd == 1) {                 
        putbuf(buffer, size);      
        return (int)size;
    }
      
    if (fd <= 0 || fd >= FD_MAX) return -1;

    
    struct file *fp = thread_current()->fd_table[fd];
    if (!fp) return -1;

    lock_acquire(&filesys_lock);
    int n = file_write(fp, buffer, size); 
    lock_release(&filesys_lock);
    return n;
}

void
sys_seek(int fd, unsigned position) 
{
    if (fd < 2)
        return;

    struct thread *t = thread_current();
	struct file *fp = t->fd_table[fd];

	if (fp == NULL) return;
    file_seek(fp, position);
}


unsigned
sys_tell(int fd) 
{
    struct thread *t = thread_current();
	struct file *fp = t->fd_table[fd];

    if (fd < 3 || fp == NULL)
        return -1;

    return file_tell(fp);
}


void sys_close (int fd){
	if(fd<2 || fd >= FD_MAX) return;
	struct thread *t = thread_current();
	struct file *fp = t->fd_table[fd];
	if (fp == NULL) return;

	// fd테이블은 해당 프로세스만의 것이기 때문에 경쟁이 발생하지 않아서 락 필요없음
	t->fd_table[fd] = NULL;

	file_close(fp);

}

// 중복이 되지 않는 가장 작은 양수 파일 디스크립터를 반환
// 열 수 없는 파일이라면 -1을 반환
// 0번과 1번은 콘솔용으로 예약되어 있습니다.
// 각 프로세스는 독립적인 fd테이블을 가지며 자식 프로세스에게 상속
// 동일 파일에 대한 서로 다른 fd는 각자 독립적으로 close되어야 하며, 파일 위치도 공유하지 않는다.
