/* Passes a bad pointer to the create system call,
   which must cause the process to be terminated with exit code
   -1. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("create(0x20101234): %d", create ((char *) 0x20101234, 0));
}

/* 유효하지 않은 주소일 시 파일 생성 실패 */
/* 파일 생성 실패해야 테스트 통과 */