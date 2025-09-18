/* Try writing to fd 0 (stdin), 
   which may just fail or terminate the process with -1 exit
   code. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  char buf = 123;
  write (0, &buf, 1);
}

/* 표준 입력에 쓰기 작업을 수행하려 함. */
/* 종료 코드 -1과 함께 실패하거나 프로세스 종료되어야 테스트 통과 */