/* This program attempts to read kernel memory. 
   This should terminate the process with a -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("Congratulations - you have successfully read kernel memory: %d", 
        *(int *)0x8004000000);
  fail ("should have exited with -1");
}

/* 커널 메모리를 읽으려 시도한다. */
/* 종료 코드 -1과 함께 프로세스 종료시키면 테스트 통과 */