/* Wait for a process that will be killed for bad behavior. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  pid_t child;
  if ((child = fork ("child-bad"))){
    msg ("wait(exec()) = %d", wait (child));
  } else {
    exec ("child-bad");
  }
}

/* 오류로 인해 강제 종료될 자식 프로세스를 기다리는 테스트 */
/* 부모의 wait() 호출이 자식의 종료 상태 -1을 정상적으로 반환받으면 테스트 통과 */