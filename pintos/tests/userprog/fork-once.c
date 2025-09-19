/* Forks and waits for a single child process. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int pid;

  if ((pid = fork("child"))){
    int status = wait (pid);
    msg ("Parent: child exit status is %d", status);
  } else {
    msg ("child run");
    exit(81);
  }
}


/* 자식 프로세스 하나를 생성하면 테스트 통과 */
/* wait을 사용하기 때문에 wait 시스템 콜도 구현해야 한다.. */