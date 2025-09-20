/* This program attempts to read memory at an address that is not mapped.
   This should terminate the process with a -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("Congratulations - you have successfully dereferenced NULL: %d", 
        *(int *)NULL);
  fail ("should have exited with -1");
}

/* 매핑되지 않은 주소의 메모리를 읽으려 시도한다. */
/* 종료 코드 -1과 함께 프로세스 종료시키면 테스트 통과 */