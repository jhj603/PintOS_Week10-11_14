#include "userprog/syscall.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include <stdio.h>
#include <syscall-nr.h>
/** project2-System Call */
#include "devices/input.h"
#include "lib/kernel/console.h"
#include "threads/mmu.h"
static tid_t sys_fork(const char *name, struct intr_frame *f);
static int sys_exec(const char *file);
static int sys_wait(tid_t tid);
static bool sys_create(const char *file, unsigned initial_size);
static bool sys_remove(const char *file);
static int sys_open(const char *file); // TODO: FD 테이블 구현 필요
static int sys_filesize(int fd);       // TODO
static int sys_read(int fd, void *buf, unsigned size);        // TODO
static int sys_write(int fd, const void *buf, unsigned size); // TODO
static void sys_seek(int fd, unsigned pos);                   // TODO
static unsigned sys_tell(int fd);                             // TODO
static void sys_close(int fd);                                // TODO
void syscall_entry(void);
void syscall_handler(struct intr_frame *);

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

void syscall_init(void)
{
    write_msr(MSR_STAR,
              ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* The interrupt service rountine should not serve any interrupts
     * until the syscall_entry swaps the userland stack to the kernel
     * mode stack. Therefore, we masked the FLAG_FL. */
    write_msr(MSR_SYSCALL_MASK,
              FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f)
{
    int sys_number = f->R.rax;

    switch (sys_number)
    {
    case SYS_HALT:
        halt();
        break;
    case SYS_EXIT:
        exit(f->R.rdi);
        break;
    case SYS_FORK:
        f->R.rax = sys_fork((const char *)f->R.rdi, f);
        break;
    case SYS_EXEC:
        f->R.rax = sys_exec((const char *)f->R.rdi);
        break;
    case SYS_WAIT:
        f->R.rax = sys_wait((tid_t)f->R.rdi);
        break;
    case SYS_CREATE:
        f->R.rax = sys_create((const char *)f->R.rdi, (unsigned)f->R.rsi);
        break;
    case SYS_REMOVE:
        f->R.rax = sys_remove((const char *)f->R.rdi);
        break;
    case SYS_OPEN:
        f->R.rax = sys_open((const char *)f->R.rdi);
        break;
    case SYS_FILESIZE:
        f->R.rax = sys_filesize((int)f->R.rdi);
        break;
    case SYS_READ:
        f->R.rax =
            sys_read((int)f->R.rdi, (void *)f->R.rsi, (unsigned)f->R.rdx);
        break;
    case SYS_WRITE:
        f->R.rax = sys_write((int)f->R.rdi, (const void *)f->R.rsi,
                             (unsigned)f->R.rdx);
        break;
    case SYS_SEEK:
        sys_seek((int)f->R.rdi, (unsigned)f->R.rsi); // ← SEEK는 위치 설정
        break;
    case SYS_TELL:
        f->R.rax = sys_tell((int)f->R.rdi); // ← TELL은 현재 위치 반환
        break;
    case SYS_CLOSE:
        sys_close((int)f->R.rdi);
        break;
    default:
        exit(-1);
    }
    // thread_exit();
}
/** project2-System Call */
void check_address(void *addr)
{
    if (is_kernel_vaddr(addr) || addr == NULL ||
        pml4_get_page(thread_current()->pml4, addr) == NULL)
        exit(-1);
}

void halt(void)
{
    power_off();
}

void exit(int status)
{
    struct thread *t = thread_current();
    t->exit_status = status;
    printf("%s: exit(%d)\n", t->name, t->exit_status);
    thread_exit();
}

bool create(const char *file, unsigned initial_size)
{
    check_address(file);

    return filesys_create(file, initial_size);
}
bool remove(const char *file)
{
    check_address(file);

    return filesys_remove(file);
}
static tid_t sys_fork(const char *name, struct intr_frame *f)
{
    check_address((void *)name);
    return process_fork(name, f);
}
static int sys_exec(const char *file)
{
    check_address((void *)file);
    return process_exec((void *)file);
}
static int sys_wait(tid_t tid)
{
    return process_wait(tid);
}
static bool sys_create(const char *file, unsigned initial_size)
{
    check_address((void *)file);
    return filesys_create(file, initial_size);
}
static bool sys_remove(const char *file)
{
    check_address((void *)file);
    return filesys_remove(file);
}

// ---- 아래 6개는 나중에 진짜 구현 필요 (지금은 더미로 컴파일만 되게) ----
static int sys_open(const char *file)
{
    check_address((void *)file);
    // TODO: filesys_open + FD 테이블 등록
    return -1;
}
static int sys_filesize(int fd)
{
    // TODO: fd → struct file* 찾아서 file_length()
    return -1;
}
static int sys_read(int fd, void *buf, unsigned size)
{
    check_address(buf);
    // TODO: fd == 0 (STDIN) 처리, 그 외 fd는 file_read()
    return -1;
}
static int sys_write(int fd, const void *buf, unsigned size)
{
    check_address((void *)buf);
    if (size == 0)
        return 0;

    if (fd == 1)
    {                                    // STDOUT
        putbuf((const char *)buf, size); // 콘솔 출력
        return (int)size;
    }
    // TODO: 파일 FD는 나중에 FD 테이블 통해 file_write()로
    return -1;
}
static void sys_seek(int fd, unsigned pos)
{
    // TODO: file_seek()
}
static unsigned sys_tell(int fd)
{
    // TODO: file_tell()
    return 0;
}
static void sys_close(int fd)
{
    // TODO: file_close() + FD 테이블에서 제거
}