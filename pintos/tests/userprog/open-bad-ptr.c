/* Passes an invalid pointer to the open system call.
   The process must be terminated with -1 exit code. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("open(0x20101234): %d", open ((char *) 0x20101234));
  fail ("should have called exit(-1)");
}

/* 유효 주소인지 검증해서 종료 코드 -1을 반환하면서 */
/* 종료되어야 테스트 통과 */