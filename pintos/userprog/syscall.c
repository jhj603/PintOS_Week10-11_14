#include "userprog/syscall.h"
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

static struct lock filesys_lock; 

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static int sys_open (const char *user_fname);
void sys_close (int fd); 
void sys_exit (int status); 
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, const void *buffer, unsigned size);
bool sys_create(const char *file, unsigned initial_size);
int filesize (int fd);


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
/* user 문자열 ustr을 커널 buf로 최대 kcap 바이트까지 복사.
   성공하면 true, 잘못된 포인터거나 너무 길면 false */
static bool copyin_string (const char *ustr, char *buf, size_t kcap) {
  if (ustr == NULL) return false;
  size_t i = 0;
  while (i + 1 < kcap) {
    /* pml4_get_page()로 주소가 매핑돼 있는지 확인 */
    if (pml4_get_page(thread_current()->pml4, (void *)ustr) == NULL)
      return false;

    char c = *(const char *)ustr;
    buf[i++] = c;
    if (c == '\0') {
      return true;  // 성공적으로 끝
    }
    ustr++;
  }
  return false;  // 널 못 만남 (너무 김)
}


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
		// f->R.rax = fork(f->R.rdi);
		break;
	case SYS_WAIT:
		// f->R.rax = process_wait(f->R.rdi);
		break;		
	case SYS_CREATE:
		f->R.rax = sys_create(f->R.rdi,f->R.rsi);
		break;
	case SYS_REMOVE:
		// f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = sys_open((const char *) f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = sys_filesize(f->R.rdi);
		break;
	case SYS_READ:
        f->R.rax = sys_read(f->R.rdi, f->R.rsi, f->R.rdx);
        break;
	case SYS_WRITE:
        f->R.rax = sys_write(f->R.rdi, f->R.rsi, f->R.rdx);
        break;
    case SYS_SEEK:
        // seek(f->R.rdi, f->R.rsi);
        break;
    case SYS_TELL:
        // f->R.rax = tell(f->R.rdi);
        break;
    case SYS_CLOSE:
        sys_close(f->R.rdi);
        break;
    default:
        sys_exit(-1);
}
}

void 
check_address (void *addr){
    if (is_kernel_vaddr(addr) || addr == NULL || pml4_get_page(thread_current()->pml4, addr) == NULL)
        sys_exit(-1);
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

bool 
sys_create(const char *file, unsigned initial_size) 
{
    check_address_range(file,initial_size);
	check_address(file);
    return filesys_create(file, initial_size);
}

bool 
remove(const char *file) 
{
    check_address(file);
    return filesys_remove(file);
}

int
sys_filesize (int fd){
	if (fd==1) return -1; 
	if(fd < 0 || fd >= FD_MAX) return -1;

	struct thread *t = thread_current();
	struct file *fp = t->fd_table[fd]; 

	lock_acquire(&filesys_lock);
	off_t len = file_length(fp);
	lock_release(&filesys_lock);

	return len;

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
    if (fd == 0) return -1;        
    if (fd < 0 || fd >= FD_MAX) return -1;

    
    struct file *fp = thread_current()->fd_table[fd];
    if (!fp) return -1;

    lock_acquire(&filesys_lock);
    int n = file_write(fp, buffer, size); 
    lock_release(&filesys_lock);
    return n;
}



void sys_close (int fd){
	if(fd<2 || fd >= FD_MAX) return;

	struct thread *t = thread_current();

	struct file *fp = t->fd_table[fd];
	if (fp == NULL) return;

	// fd테이블은 해당 프로세스만의 것이기 때문에 경쟁이 발생하지 않아서 락 필요없음
	t->fd_table[fd] = NULL;

	// 대신 파일 시스템을 건드릴 때에는 경쟁이 발생 할 수 있기 때문에 반드시 락 걸어야 함
	lock_acquire(&filesys_lock);
	file_close(fp);
	lock_release(&filesys_lock);

	if (fd < t->next_fd) t->next_fd = fd;

}

// 중복이 되지 않는 가장 작은 양수 파일 디스크립터를 반환
// 열 수 없는 파일이라면 -1을 반환
// 0번과 1번은 콘솔용으로 예약되어 있습니다.
// 각 프로세스는 독립적인 fd테이블을 가지며 자식 프로세스에게 상속
// 동일 파일에 대한 서로 다른 fd는 각자 독립적으로 close되어야 하며, 파일 위치도 공유하지 않는다.

static int
sys_open (const char *user_fname) {

	if (user_fname == NULL) return -1;
	/* 유저가 요청하는 파일 명이 올바른지 확인하고 새로운 배열에 저장*/
	char kfname[256];
  	if (!copyin_string(user_fname, kfname, sizeof kfname))
    	sys_exit(-1);


	if (kfname[0] == '\0') {
  	return -1;  // 빈 파일명도 사용자 오류 취급
	}
	/* 락 잠그고 실제 파일 객체를 연다 작업 끝나면 락 해제 */
  	lock_acquire(&filesys_lock);
	struct file *fp = filesys_open(kfname);
	lock_release(&filesys_lock);

	if (fp == NULL) {
		return -1;
	}


	struct thread *t = thread_current();
	int start = t->next_fd;
	int fd = -1;

	for (int i = 0; i < FD_MAX - FD_MIN; i++) {
	int idx = FD_MIN + ((start - FD_MIN + i) % (FD_MAX - FD_MIN));
	if (t->fd_table[idx] == NULL) { fd = idx; break; }
	}

	if (fd == -1) {
	lock_acquire(&filesys_lock);
	file_close(fp);
	lock_release(&filesys_lock);
	return -1;
	}

	
	t->fd_table[fd] = fp;
	t->next_fd = (fd + 1 < FD_MAX) ? fd + 1 : FD_MIN;

	return fd;


  }
