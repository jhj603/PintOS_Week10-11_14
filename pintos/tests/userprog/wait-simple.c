/* Wait for a subprocess to finish. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int pid;
  if ((pid = fork ("child-simple"))){
    msg ("wait(exec()) = %d", wait (pid));
  } else {
    exec ("child-simple");
  }
}

/* 자식 프로세스가 끝나길 기다리면 테스트 통과 */