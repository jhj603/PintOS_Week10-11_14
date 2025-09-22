#include "userprog/process.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);

/* General process initializer for initd and other process. */
static void process_init(void)
{
    struct thread *current = thread_current();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t process_create_initd(const char *file_name)
{
    char *fn_copy;
    tid_t tid;

    /* Make a copy of FILE_NAME.
     * Otherwise there's a race between the caller and load(). */
    fn_copy = palloc_get_page(0);
    if (fn_copy == NULL)
        return TID_ERROR;
    strlcpy(fn_copy, file_name, PGSIZE);

    /** project2-System Call */
    char *ptr;
    strtok_r(file_name, " ", &ptr);

    /* Create a new thread to execute FILE_NAME. */
    tid = thread_create(file_name, PRI_DEFAULT, initd, fn_copy);
    if (tid == TID_ERROR)
        palloc_free_page(fn_copy);
    return tid;
}

/* A thread function that launches first user process. */
static void initd(void *f_name)
{
#ifdef VM
    supplemental_page_table_init(&thread_current()->spt);
#endif

    process_init();

    if (process_exec(f_name) < 0)
        PANIC("Fail to launch initd\n");
    NOT_REACHED();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED)
{
    struct thread *curr =
        thread_current(); // 현재 실행 중인(부모) 스레드 포인터

    struct intr_frame *f = // 부모 커널스택의 사용자 컨텍스트가 저장된 위치(맨
                           // 위 intr_frame)의 주소 계산
        (pg_round_up(rrsp()) -
         sizeof(struct intr_frame)); // rrsp(): 현재 RSP 값, 페이지 경계로 올림
                                     // 뒤 intr_frame 크기만큼 빼면 저장 위치
    memcpy(
        &curr->parent_if, f,
        sizeof(
            struct intr_frame)); // 부모의 intr_frame 스냅샷을 curr->parent_if에
                                 // 복사(자식에게 넘겨주기 위함)

    /* 현재 스레드를 새 스레드로 복제합니다.*/
    tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork,
                              curr); // 새 스레드(자식) 생성, 시작함수는
                                     // __do_fork, aux로 부모 포인터 전달

    if (tid == TID_ERROR) // 생성 실패하면
        return TID_ERROR; // 바로 에러 반환

    struct thread *child =
        get_child_process(tid); // 부모의 child_list에서 방금 만든 자식의 thread
                                // 구조체 포인터 찾기

    sema_down(&child->fork_sema); // 자식이 __do_fork에서 메모리/파일 등 자원
                                  // 복제를 끝내고 fork_sema를 up 할 때까지
                                  // 부모는 여기서 대기(동기화 지점)

    if (child->exit_status ==
        TID_ERROR)        // 자식이 복제 과정에서 실패 표시(TID_ERROR) 했으면
        return TID_ERROR; // 부모도 fork 실패로 간주하고 에러 반환

    return tid; // 성공 시 부모는 자식의 tid(=pid 역할)를 반환
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool duplicate_pte(uint64_t *pte, void *va, void *aux)
{
    struct thread *current = thread_current();
    struct thread *parent = (struct thread *)aux;
    void *parent_page;
    void *newpage;
    bool writable;

    /* 1. parent_page가 커널 영역 주소라면 복제할 필요 없음 → 바로 true 반환 */
    if (is_kernel_vaddr(va))
        return true;

    /* 2. 부모의 pml4(페이지 맵 레벨 4)에서 va(가상 주소)에 해당하는 물리
     * 페이지를 찾음 */
    parent_page = pml4_get_page(parent->pml4, va);
    if (parent_page == NULL)
        return false; // 부모에게도 없는 페이지라면 실패

    /* 3. 자식 프로세스를 위해 새로운 사용자 영역 페이지 할당 */
    newpage = palloc_get_page(PAL_ZERO); // PAL_ZERO → 새 페이지를 0으로 초기화
    if (newpage == NULL)
        return false; // 메모리 부족 시 실패

    /* 4. 부모의 물리 페이지 내용을 새로 할당한 페이지(newpage)로 복사 */
    memcpy(newpage, parent_page, PGSIZE); // 한 페이지(4KB) 단위 복제
    writable = is_writable(pte); // 부모 페이지 권한 확인 (쓰기 가능 여부)

    /* 5. 자식 프로세스의 pml4에 새로운 페이지 매핑
          va 가상 주소 → newpage 물리 페이지
          권한은 부모와 동일(writable)하게 설정 */
    if (!pml4_set_page(current->pml4, va, newpage, writable))
    {
        /* 6. 매핑 실패 시 (예: 이미 매핑되어 있음, 메모리 부족 등)
              → 복사한 newpage는 아직 등록 안 됐으므로 그대로 두면 메모리 누수
              → 여기선 단순히 false 반환 (실제로는 palloc_free_page(newpage)
           하는게 안전) */
        return false;
    }
    return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork
 * to this function. */
static void __do_fork(void *aux)
{
    struct intr_frame
        if_; // 자식 프로세스가 사용할 CPU 레지스터 저장용 intr_frame
    struct thread *parent = (struct thread *)
        aux; // 부모 프로세스의 thread 구조체 (process_fork에서 넘겨준 값)
    struct thread *current = thread_current(); // 현재 실행 중인 스레드 = 새로
                                               // 만들어진 자식 프로세스 자신

    /* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
    struct intr_frame *parent_if = &parent->parent_if;
    // 부모가 process_fork()에서 저장해둔 intr_frame (CPU 레지스터 상태).
    // 자식이 시작할 때 이걸 복사해서 자기 레지스터 초기 상태로 사용.

    bool succ =
        true; // fork 성공 여부를 추적할 플래그 (중간에 실패하면 false 처리)

    /* 1. Read the cpu context to local stack. */
    memcpy(&if_, parent_if, sizeof(struct intr_frame));
    // 부모의 intr_frame을 통째로 자식의 지역 변수 if_로 복사.
    // 즉, 부모가 실행 중이던 레지스터 상태를 자식에게 그대로 물려줌.

    if_.R.rax = 0;
    // fork()의 규약: 부모는 자식 PID를 반환하고,
    // 자식은 항상 0을 반환해야 함.
    // 그래서 자식 intr_frame의 RAX 레지스터에 0을 세팅.

    /* 2. Duplicate PT */
    current->pml4 = pml4_create();
    // 자식용 새 페이지 테이블(pml4)을 생성.
    // 부모와 같은 주소 공간을 쓸 수 없으니 자식만의 pml4가 필요.

    if (current->pml4 == NULL)
        goto error;
    // 메모리 부족으로 pml4 생성 실패 → 바로 에러 처리.

    process_activate(current);
    // CPU에 현재 자식의 페이지 테이블을 활성화시킴.
    // 이제부터는 메모리 접근이 자식 프로세스의 주소 공간 기준으로 이뤄짐.

#ifdef VM
    supplemental_page_table_init(&current->spt);
    // VM(Project 3) 버전: 자식의 보조 페이지 테이블(SPT) 초기화.

    if (!supplemental_page_table_copy(&current->spt, &parent->spt))
        goto error;
    // 부모의 SPT(메모리 매핑 정보)를 자식에게 복사.
    // 실패하면 에러 처리.
#else
    if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
        goto error;
    // VM이 아닐 경우(Project 2): 부모의 모든 페이지 테이블 엔트리를 순회하면서
    // 자식 pml4에 페이지 복사. (duplicate_pte 함수 사용)
#endif

    /* TODO: Your code goes here.
     * TODO: Hint) To duplicate the file object, use `file_duplicate`
     * TODO:       in include/filesys/file.h. Note that parent should not
     * return
     * TODO:       from the fork() until this function successfully
     * duplicates
     * TODO:       the resources of parent.*/

    if (parent->fd_idx >=
        FDCOUNT_LIMIT) // 부모의 파일 디스크립터 개수가 한도를 초과하면
        goto error;    // 더 이상 복제 불가 → 에러 처리

    current->fd_idx =
        parent
            ->fd_idx; // 자식의 fd 인덱스(열린 파일 개수)를 부모와 동일하게 맞춤

    for (int fd = 3; fd < parent->fd_idx;
         fd++) // 0,1,2(stdin,stdout,stderr)를 제외하고 3번부터 복제 시작
    {
        if (parent->fdt[fd] == NULL) // 부모 fdt의 해당 fd가 비어 있으면
            continue;                // 건너뜀

        current->fdt[fd] = file_duplicate( // 부모가 연 파일 객체를 복제해서
            parent->fdt[fd]);              // 자식의 fdt[fd]에 저장
    }

    sema_up(
        &current
             ->fork_sema); // 자식 초기화가 끝났음을 부모에게 알림(sema unblock)

    process_init(); // 새 프로세스용 초기화 (현재는 빈 함수, 후속 확장 대비)

    if (succ)          // 여기까지 성공적으로 왔다면
        do_iret(&if_); // 자식 프로세스로 전환 (유저모드 실행 시작)

error:
    sema_up(&current->fork_sema); // 에러 발생 시에도 부모가 기다리지 않도록
                                  // 세마포어 해제
    exit(TID_ERROR);              // 자식 프로세스 비정상 종료 처리
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int process_exec(void *f_name)
{
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

    // 공백 기준으로 file_name 버퍼를 파싱한다.
    // 첫 호출: "프로그램명" 토큰을 꺼내오고, 내부적으로 공백을 '\0'로
    // 바꿔서 문자열을 분할한다.
    for (arg = strtok_r(file_name, " ", &ptr); arg != NULL;
         arg = strtok_r(NULL, " ", &ptr))
        argv[argc++] = arg;

    /* And then load the binary */
    success = load(file_name, &if_);

    /* If load failed, quit. */

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
int process_wait(tid_t child_tid UNUSED)
{
    /* 부모 프로세스가 특정 자식(child_tid)의 종료를 기다리고
       그 자식의 exit status를 반환한다. 조건에 맞지 않으면 -1 반환. */

    struct thread *child =
        get_child_process(child_tid); // 부모의 자식 리스트에서 child_tid를
                                      // 가진 자식 스레드 찾기
    if (child == NULL)            // 자식이 아니거나 이미 wait()로 수거된 경우
        return -1;                // 규격상 즉시 -1 반환
    sema_down(&child->wait_sema); // 자식이 종료할 때까지 대기(자식이
                                  // process_exit()에서 up 해줌)

    int exit_status =
        child->exit_status; // 자식이 종료하면서 남긴 종료코드 백업

    list_remove(&child->child_elem); // 중복 wait 방지: 부모의
                                     // child_list에서 자식 노드 제거

    sema_up(&child->exit_sema); // 자식이 최종 정리 진행하도록 신호(자식이
                                // exit_sema에서 대기하는 설계일 때 의미 있음)

    return exit_status; // 부모는 자식의 종료코드를 반환
}

void process_exit(void)
{
    struct thread *curr = thread_current();
    /* TODO: Your code goes here.
     * TODO: Implement process termination message (see
     * TODO: project2/process_termination.html).
     * TODO: We recommend you to implement process resource cleanup here. */

    for (int fd = 0; fd < curr->fd_idx; fd++) // FDT 비우기
        close(fd);

    file_close(curr->runn_file); // 현재 프로세스가 실행중인 파일 종료

    palloc_free_multiple(curr->fdt, FDT_PAGES);

    process_cleanup();

    sema_up(&curr->wait_sema); // 자식 프로세스가 종료될 때까지 대기하는
                               // 부모에게 signal

    sema_down(&curr->exit_sema); // 부모 프로세스가 종료될 떄까지 대기
}

/* Free the current process's resources. */
static void process_cleanup(void)
{
    struct thread *curr = thread_current();

#ifdef VM
    supplemental_page_table_kill(&curr->spt);
#endif

    uint64_t *pml4;
    /* Destroy the current process's page directory and switch back
     * to the kernel-only page directory. */
    pml4 = curr->pml4;
    if (pml4 != NULL)
    {
        /* Correct ordering here is crucial.  We must set
         * cur->pagedir to NULL before switching page directories,
         * so that a timer interrupt can't switch back to the
         * process page directory.  We must activate the base page
         * directory before destroying the process's page
         * directory, or our active page directory will be one
         * that's been freed (and cleared). */
        curr->pml4 = NULL;
        pml4_activate(NULL);
        pml4_destroy(pml4);
    }
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next)
{
    /* Activate thread's page tables. */
    pml4_activate(next->pml4);

    /* Set thread's kernel stack for use in processing interrupts. */
    tss_update(next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr
{
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

struct ELF64_PHDR
{
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

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool load(const char *file_name, struct intr_frame *if_)
{
    struct thread *t = thread_current();
    struct ELF ehdr;
    struct file *file = NULL;
    off_t file_ofs;
    bool success = false;
    int i;

    /* Allocate and activate page directory. */
    t->pml4 = pml4_create();
    if (t->pml4 == NULL)
        goto done;
    process_activate(thread_current());

    /* Open executable file. */
    file = filesys_open(file_name);
    if (file == NULL)
    {
        printf("load: %s: open failed\n", file_name);
        goto done;
    }

    /** #Project 2: System Call - 파일 실행 명시 및 접근 금지 설정  */
    t->runn_file = file;
    file_deny_write(file); /** #Project 2: Denying Writes to Executables */

    /* Read and verify executable header. */
    if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
        memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 ||
        ehdr.e_machine != 0x3E // amd64
        || ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) ||
        ehdr.e_phnum > 1024)
    {
        printf("load: %s: error loading executable\n", file_name);
        goto done;
    }

    /* Read program headers. */
    file_ofs = ehdr.e_phoff;
    for (i = 0; i < ehdr.e_phnum; i++)
    {
        struct Phdr phdr;

        if (file_ofs < 0 || file_ofs > file_length(file))
            goto done;
        file_seek(file, file_ofs);

        if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
            goto done;
        file_ofs += sizeof phdr;
        switch (phdr.p_type)
        {
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
        case PT_LOAD:
            if (validate_segment(&phdr, file))
            {
                bool writable = (phdr.p_flags & PF_W) != 0;
                uint64_t file_page = phdr.p_offset & ~PGMASK;
                uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
                uint64_t page_offset = phdr.p_vaddr & PGMASK;
                uint32_t read_bytes, zero_bytes;
                if (phdr.p_filesz > 0)
                {
                    /* Normal segment.
                     * Read initial part from disk and zero the rest. */
                    read_bytes = page_offset + phdr.p_filesz;
                    zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) -
                                  read_bytes);
                }
                else
                {
                    /* Entirely zero.
                     * Don't read anything from disk. */
                    read_bytes = 0;
                    zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
                }
                if (!load_segment(file, file_page, (void *)mem_page, read_bytes,
                                  zero_bytes, writable))
                    goto done;
            }
            else
                goto done;
            break;
        }
    }

    /* Set up stack. */
    if (!setup_stack(if_))
        goto done;

    /* Start address. */
    if_->rip = ehdr.e_entry;

    success = true;

done:
    /* We arrive here whether the load is successful or not. */
    // file_close(file);

    return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Phdr *phdr, struct file *file)
{
    /* p_offset and p_vaddr must have the same page offset. */
    if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
        return false;

    /* p_offset must point within FILE. */
    if (phdr->p_offset > (uint64_t)file_length(file))
        return false;

    /* p_memsz must be at least as big as p_filesz. */
    if (phdr->p_memsz < phdr->p_filesz)
        return false;

    /* The segment must not be empty. */
    if (phdr->p_memsz == 0)
        return false;

    /* The virtual memory region must both start and end within the
       user address space range. */
    if (!is_user_vaddr((void *)phdr->p_vaddr))
        return false;
    if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
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
static bool install_page(void *upage, void *kpage, bool writable);

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
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable)
{
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);

    file_seek(file, ofs);
    while (read_bytes > 0 || zero_bytes > 0)
    {
        /* Do calculate how to fill this page.
         * We will read PAGE_READ_BYTES bytes from FILE
         * and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* Get a page of memory. */
        uint8_t *kpage = palloc_get_page(PAL_USER);
        if (kpage == NULL)
            return false;

        /* Load this page. */
        if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
        {
            palloc_free_page(kpage);
            return false;
        }
        memset(kpage + page_read_bytes, 0, page_zero_bytes);

        /* Add the page to the process's address space. */
        if (!install_page(upage, kpage, writable))
        {
            printf("fail\n");
            palloc_free_page(kpage);
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
static bool setup_stack(struct intr_frame *if_)
{
    uint8_t *kpage;
    bool success = false;

    kpage = palloc_get_page(PAL_USER | PAL_ZERO);
    if (kpage != NULL)
    {
        success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
        if (success)
            if_->rsp = USER_STACK;
        else
            palloc_free_page(kpage);
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
static bool install_page(void *upage, void *kpage, bool writable)
{
    struct thread *t = thread_current();

    /* Verify that there's not already a page at that virtual
     * address, then map our page there. */
    return (pml4_get_page(t->pml4, upage) == NULL &&
            pml4_set_page(t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on
 * the upper block. */

static bool lazy_load_segment(struct page *page, void *aux)
{
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
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable)
{
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);

    while (read_bytes > 0 || zero_bytes > 0)
    {
        /* Do calculate how to fill this page.
         * We will read PAGE_READ_BYTES bytes from FILE
         * and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* TODO: Set up aux to pass information to the lazy_load_segment. */
        void *aux = NULL;
        if (!vm_alloc_page_with_initializer(VM_ANON, upage, writable,
                                            lazy_load_segment, aux))
            return false;

        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
    }
    return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool setup_stack(struct intr_frame *if_)
{
    bool success = false;
    void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

    /* TODO: Map the stack on stack_bottom and claim the page immediately.
     * TODO: If success, set the rsp accordingly.
     * TODO: You should mark the page is stack. */
    /* TODO: Your code goes here */

    return success;
}
#endif /* VM */

/** project2-Command Line Parsing */
// 유저 스택에 파싱된 토큰을 저장하는 함수
void argument_stack(char **argv, int argc, struct intr_frame *if_)
{
    char *arg_addr[100];

    /* 1) 문자열을 역순으로 복사 (+1로 '\0' 포함) */
    for (int i = argc - 1; i >= 0; i--)
    {
        size_t len = strlen(argv[i]) + 1; // 반드시 +1
        if_->rsp -= len;
        memcpy((void *)if_->rsp, argv[i], len);
        arg_addr[i] = (char *)if_->rsp; // 문자열 시작주소 저장
    }

    /* 2) 8바이트 정렬 (과제 가이드에 맞추어 8 사용) */
    while ((if_->rsp % 8) != 0)
    {
        if_->rsp -= 1;
        *(uint8_t *)(uintptr_t)if_->rsp = 0;
    }

    /* 3) argv[argc] = NULL (센티넬 딱 한 번) */
    if_->rsp -= sizeof(char *);
    *(char **)(void *)if_->rsp = NULL;

    /* 4) argv 포인터들을 역순으로 푸시 */
    for (int i = argc - 1; i >= 0; i--)
    {
        if_->rsp -= sizeof(char *);
        memcpy((void *)if_->rsp, &arg_addr[i], sizeof(char *));
    }

    /* 5) 이제 rsp가 argv의 시작을 가리킴 */
    char **argv_on_stack = (char **)if_->rsp;

    /* 6) fake return address */
    if_->rsp -= sizeof(void *);
    memset((void *)if_->rsp, 0, sizeof(void *));

    /* 7) 레지스터에 argc/argv 전달 (SysV AMD64) */
    if_->R.rdi = argc;
    if_->R.rsi = (uint64_t)argv_on_stack;
}

/** project2-System Call */
struct thread *get_child_process(int pid)
{
    struct thread *curr = thread_current();
    struct thread *t;

    for (struct list_elem *e = list_begin(&curr->child_list);
         e != list_end(&curr->child_list); e = list_next(e))
    {
        t = list_entry(e, struct thread, child_elem);

        if (pid == t->tid)
            return t;
    }

    return NULL;
}

int process_add_file(struct file *f)
{
    struct thread *t = thread_current();
    if (f == NULL || t->fdt == NULL)
        return -1;

    // 1) fd_idx부터 끝까지 빈칸 검색
    for (int i = t->fd_idx; i < FDCOUNT_LIMIT; i++)
    {
        if (t->fdt[i] == NULL)
        {
            t->fdt[i] = f;
            t->fd_idx = i + 1; // 다음 검색 시작점 갱신
            return i;          // 할당된 fd 반환
        }
    }

    // 2) 앞쪽(2부터 fd_idx 전까지)에서 빈칸 다시 검색
    for (int i = 2; i < t->fd_idx; i++)
    {
        if (t->fdt[i] == NULL)
        {
            t->fdt[i] = f;
            return i;
        }
    }

    // 3) 꽉 찼으면 실패
    return -1;
}

struct file *process_get_file(int fd)
{
    struct thread *t = thread_current();
    if (fd < 0 || fd >= FDCOUNT_LIMIT || t->fdt == NULL)
        return NULL;
    return t->fdt[fd];
}

int process_close_file(int fd)
{
    struct thread *curr = thread_current();

    if (fd >= FDCOUNT_LIMIT)
        return -1;

    curr->fdt[fd] = NULL;
    return 0;
}