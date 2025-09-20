/* This program attempts to execute code at address 0, which is not mapped.
   This should terminate the process with a -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("Congratulations - you have successfully called NULL: %d", 
        ((int (*)(void))NULL)());
  fail ("should have exited with -1");
}

/* 매핑되지 않은 0번 주소의 코드를 실행시키려 한다. */
/* 종료 코드 -1과 함께 프로세스 종료시키면 테스트 통과 */