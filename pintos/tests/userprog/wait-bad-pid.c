/* Waits for an invalid pid.  This may fail or terminate the
   process with -1 exit code. */

#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  wait ((pid_t) 0x0c020301);
}

/* 유효하지 않은 pid를 기다림 */
/* 종료 코드 -1과 함께 실패하거나 프로세스 종료하면 테스트 통과*/