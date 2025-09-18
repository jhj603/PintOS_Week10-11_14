/* Passes an invalid pointer to the read system call.
   The process must be terminated with -1 exit code. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle;
  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");

  read (handle, (char *) 0xc0100000, 123);
  fail ("should not have survived read()");
}

/* 파일 오픈 후 read 요청 시 유효하지 않은 포인터일 경우 */
/* 종료 코드 -1을 반환하면서 프로세스 종료되어야 테스트 통과 */