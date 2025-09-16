/* Tries to open a file with the empty string as its name. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle = open ("");
  if (handle != -1)
    fail ("open() returned %d instead of -1", handle);
}

/* 빈 문자열 이름으로 파일 열기 시도 */
/* -1을 반환해 파일 열기 실패해야 테스트 통과 */