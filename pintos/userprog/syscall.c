#include "userprog/syscall.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include <stdio.h>
#include <syscall-nr.h>
/** filesys & mmu */
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/off_t.h"
#include "lib/string.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "userprog/process.h"
/* 전원 종료 & 키보드 입력 & 콘솔 출력 */
#include "devices/input.h"
#include "lib/kernel/console.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/** project2-System Call */
struct lock filesys_lock; // 파일 읽기/쓰기용 전역 락

/* MSR 레지스터 상수 */
#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
    write_msr(MSR_STAR,
              ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* 유저 스택이 커널 스택으로 바뀌기 전까지 인터럽트가 처리되지 않도록 마스킹
     */
    write_msr(MSR_SYSCALL_MASK,
              FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

    /* filesys 공용 락 초기화 */
    lock_init(&filesys_lock);
}

/* 유저 포인터 유효성 검사: NULL 금지, 커널주소 금지, pml4 매핑 존재해야 함 */
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

pid_t fork(const char *thread_name)
{
    check_address(thread_name);

    return process_fork(thread_name, NULL);
}

/* ==== 시스템 콜 구현 프로토타입 (정의는 아래) ==== */
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned length);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
int tell(int fd);
void close(int fd);

/* 메인 시스템 콜 디스패처 */
void syscall_handler(struct intr_frame *f)
{
    int sys_number = (int)f->R.rax;
    /* 인자 레지스터 순서: rdi, rsi, rdx, r10, r8, r9 */

    switch (sys_number)
    {
    case SYS_HALT:
    {
        halt();
        break;
    }
    case SYS_EXIT:
    {
        int status = (int)f->R.rdi;
        exit(status);
        break;
    }
    case SYS_FORK:
    {
        const char *name = (const char *)f->R.rdi;
        f->R.rax = process_fork(name, f);
        break;
    }
    case SYS_EXEC:
    {
        const char *file = (const char *)f->R.rdi;
        /* 템플릿 시그니처가 void* 이므로 캐스트 */
        f->R.rax = exec(file); // ← 커널 페이지에 복사해서 넘기는 래퍼 사용
        break;
    }
    case SYS_WAIT:
    {
        tid_t child = (tid_t)f->R.rdi;
        f->R.rax = process_wait(child);
        break;
    }
    case SYS_CREATE:
    {
        const char *file = (const char *)f->R.rdi;
        unsigned size = (unsigned)f->R.rsi;
        f->R.rax = create(file, size);
        break;
    }
    case SYS_REMOVE:
    {
        const char *file = (const char *)f->R.rdi;
        f->R.rax = remove(file);
        break;
    }
    case SYS_OPEN:
    {
        const char *file = (const char *)f->R.rdi;
        f->R.rax = open(file);
        break;
    }
    case SYS_FILESIZE:
    {
        int fd = (int)f->R.rdi;
        f->R.rax = filesize(fd);
        break;
    }
    case SYS_READ:
    {
        int fd = (int)f->R.rdi;
        void *buf = (void *)f->R.rsi;
        unsigned len = (unsigned)f->R.rdx;
        f->R.rax = read(fd, buf, len);
        break;
    }
    case SYS_WRITE:
    {
        int fd = (int)f->R.rdi;
        const void *buf = (const void *)f->R.rsi;
        unsigned len = (unsigned)f->R.rdx;
        f->R.rax = write(fd, buf, len);
        break;
    }
    case SYS_SEEK:
    {
        int fd = (int)f->R.rdi;
        unsigned pos = (unsigned)f->R.rsi;
        seek(fd, pos);
        break;
    }
    case SYS_TELL:
    {
        int fd = (int)f->R.rdi;
        f->R.rax = tell(fd);
        break;
    }
    case SYS_CLOSE:
    {
        int fd = (int)f->R.rdi;
        close(fd);
        break;
    }
    default:
    {
        exit(-1);
        break;
    }
    }
}

/* ===== 시스템 콜 실제 동작 구현부 ===== */
int exec(const char *cmd_line)
{
    check_address(cmd_line);

    off_t size = strlen(cmd_line) + 1;
    char *cmd_copy = palloc_get_page(PAL_ZERO);

    if (cmd_copy == NULL)
        return -1;

    memcpy(cmd_copy, cmd_line, size);

    if (process_exec(cmd_copy) == -1)
        return -1;

    return 0;
}

int wait(pid_t tid)
{
    return process_wait(tid);
}
void halt(void)
{
    power_off();
}

void exit(int status)
{
    struct thread *t = thread_current();
    t->exit_status = status;
    printf("%s: exit(%d)\n", t->name,
           t->exit_status); // Process Termination Message
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

int open(const char *file)
{
    // [1] 유저 포인터 검증
    // - NULL, 커널 영역, 매핑 안 된 주소면 즉시 exit(-1)로 프로세스 종료
    // - open-null 테스트에서 여기서 걸려서 exit(-1)이 찍힘
    check_address(file);

    // [2] 파일 열기 시도
    // - 파일시스템에 "file" 이름으로 존재하는지 확인하고
    //   있으면 struct file* 반환, 없으면 NULL
    // - open-missing 같은 테스트에선 NULL이 리턴됨
    struct file *newfile = filesys_open(file);

    // [3] 파일이 없을 경우
    // - 존재하지 않거나 열기 실패 → -1 반환
    // - open-missing 테스트에서 여기로 들어감
    if (newfile == NULL)
        return -1;

    // [4] FD 테이블에 등록
    // - 현재 프로세스의 FDT에서 빈 칸을 찾아서 newfile을 등록하고,
    //   그 인덱스(FD)를 반환
    // - open-normal 테스트에선 여기서 FD ≥ 2를 얻음
    // - open-twice에선 같은 파일을 두 번 열어도 FDT 빈 칸을 다르게 주므로 FD가
    // 달라짐
    int fd = process_add_file(newfile);

    // [5] FD 등록 실패 시 처리
    // - FDT가 꽉 찼거나 오류가 나면 -1이 리턴됨
    // - 이때는 newfile 포인터를 닫아주지 않으면 누수가 발생하므로
    //   file_close(newfile)로 정리
    if (fd == -1)
        file_close(newfile);

    // [6] 최종 반환
    // - 성공 시 FD 반환, 실패 시 위에서 이미 -1 리턴
    return fd;
}

int filesize(int fd)
{
    struct file *file = process_get_file(fd);
    if (file == NULL)
        return -1;
    return file_length(file);
}

// fd로 지정된 대상(키보드/파일)에서 최대 length 바이트를 읽어
// 유저 버퍼(buffer)에 채우고, 실제로 읽은 바이트 수를 반환한다.
// 실패(-1) 또는 포인터 오류 시 프로세스를 종료(check_address 내부)한다.
int read(int fd, void *buffer, unsigned length)
{
    // [1] 유저 버퍼 포인터 유효성 검증
    //  - NULL, 커널 영역, 매핑되지 않은 주소면 check_address가 exit(-1) 처리.
    //  - 주의: 현재는 'buffer'의 시작 주소만 검사한다.
    //          length > 1 인 경우, buffer + length - 1 까지의 범위 검사도
    //          해주는 것이 안전하다.
    check_address(buffer);

    // [2] 표준 입력(stdin)에서 읽기: fd == 0
    if (fd == 0)
    { /* stdin */
        // 버퍼를 바이트 배열로 캐스팅
        unsigned char *buf = (unsigned char *)buffer;

        // 키보드로부터 length 바이트를 차례대로 읽어 buf에 채운다.
        //  - input_getc()는 한 글자씩 가져온다.
        //  - length가 0이면 루프는 0회 수행되고 0이 반환된다.
        for (unsigned i = 0; i < length; i++)
            buf[i] = input_getc();

        // stdin에서는 요청한 만큼 읽었다고 가정하고 length를 그대로 반환.
        // (일반적으로 테스트는 고정 바이트 수를 기대한다)
        return (int)length;
    }

    // [3] 잘못된/읽기 불가한 fd 거르기
    //  - fd < 0 : 잘못된 fd
    //  - fd < 3 : 1(stdout), 2(stderr)에서의 읽기는 금지 → -1
    //    (fd == 0은 위에서 이미 처리했으므로 여기선 1,2만 걸린다)
    if (fd < 0 || fd < 3) /* 음수 또는 stdout/stderr에서 읽기 */
        return -1;

    // [4] 일반 파일(fd >= 3): 프로세스의 파일 테이블(FDT)에서 file* 조회
    struct file *file = process_get_file(fd);
    if (file == NULL) // 닫힌 fd이거나 존재하지 않으면 실패
        return -1;

    // [5] 파일에서 읽기
    //  - 파일시스템은 전역 자원이므로 락으로 보호
    //  - file_read는 실제로 읽은 바이트 수(off_t)를 반환(EOF면 0 가능)
    off_t bytes;
    lock_acquire(&filesys_lock);
    bytes = file_read(file, buffer, length);
    lock_release(&filesys_lock);

    // [6] 읽은 바이트 수를 int로 캐스팅해 반환
    return (int)bytes;
}

int write(int fd, const void *buffer, unsigned length)
{
    check_address(buffer);

    if (fd <= 0) /* stdin에 쓰기 금지 & 음수 fd 금지 */
        return -1;

    if (fd < 3)
    { /* stdout(1), stderr(2) */
        putbuf(buffer, length);
        return (int)length;
    }

    struct file *file = process_get_file(fd);
    if (file == NULL)
        return -1;

    off_t bytes;
    lock_acquire(&filesys_lock);
    bytes = file_write(file, buffer, length);
    lock_release(&filesys_lock);
    return (int)bytes;
}

void seek(int fd, unsigned position)
{
    struct file *file = process_get_file(fd);
    if (fd < 3 || file == NULL)
        return;
    file_seek(file, position);
}

int tell(int fd)
{
    struct file *file = process_get_file(fd);
    if (fd < 3 || file == NULL)
        return -1;
    return file_tell(file);
}

void close(int fd)
{
    struct file *file = process_get_file(
        fd); // process_get_file(fd)로 현재 프로세스의 FDT에서 file* 조회
    if (fd < 3 ||
        file == NULL) // 표준입출력이면 무시하고  없으면 걍 무시하고 리턴
        return;

    process_close_file(fd); // loise 파일 호출해서 NULL로 지워버리기~
    file_close(file);       // file_close(file) 호출
}