/* Forks and waits recursively. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void fork_and_wait (void);
int magic = 1;

void
fork_and_wait (void){
  int pid;
  magic++;

  if (magic >= 10){
    exit(magic);
  }

  if ((pid = fork("child"))){
    magic++;
    int status = wait (pid);
    msg ("Parent: child exit status is %d", status);
  } else {
    msg ("child run");
    fork_and_wait();
    exit(magic);
  }
}

void
test_main (void) 
{
  fork_and_wait();
}

/* 포크하고 대기하는 걸 재귀적으로 수행할 수 있으면 테스트 통과 */
/* 중첩된 부모-자식 관계를 안정적으로 처리할 수 있는지 검증 */