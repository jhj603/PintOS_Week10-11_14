/* This program attempts to write to memory at an address that is not mapped.
   This should terminate the process with a -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  *(int *)NULL = 42;
  fail ("should have exited with -1");
}

/* 매핑되지 않은 주소 메모리에 쓰기 작업을 시도한다. */
/* 종료 코드 -1과 함께 프로세스 종료시키면 테스트 통과 */