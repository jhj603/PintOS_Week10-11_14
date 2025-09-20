/* Wait for a subprocess to finish, twice.
   The first call must wait in the usual way and return the exit code.
   The second wait call must return -1 immediately. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  pid_t child;
  if ((child = fork ("child-simple"))){
    msg ("wait(exec()) = %d", wait (child));
    msg ("wait(exec()) = %d", wait (child));
  } else {
    exec ("child-simple");
  }
}

/* 한 자식 프로세스를 두 번 기다린다. */
/* 첫 번째 wait는 정상적으로 종료된다. */
/* 두 번째 wait은 즉시 -1을 반환하면 테스트 통과 */