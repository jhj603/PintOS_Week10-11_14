/* Tries to open a file with the null pointer as its name.
   The process must be terminated with exit code -1. */

#include <stddef.h>
#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  open (NULL);
}

/* 파일 이름을 NULL로 넘겨줬을 때 종료 코드 -1을 반환하면서 */
/* 종료되어야 테스트 통과 */