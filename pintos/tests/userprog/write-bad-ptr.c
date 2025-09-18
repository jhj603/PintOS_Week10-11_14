/* Passes an invalid pointer to the write system call.
   The process must be terminated with -1 exit code. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle;
  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");

  write (handle, (char *) 0x10123420, 123);
  fail ("should have exited with -1");
}

/* 유효하지 않은 buffer 주소 값으로 write 시도 */
/* 종료 코드 -1을 반환하면서 프로세스 종료되어야 테스트 통과 */