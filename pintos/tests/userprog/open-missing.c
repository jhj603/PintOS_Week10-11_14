/* Tries to open a nonexistent file. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle = open ("no-such-file");
  if (handle != -1)
    fail ("open() returned %d", handle);
}

/* 없는 파일을 열려고 하면 -1을 반환해 실패를 알림. */
/* -1을 반환해서 실패해야 테스트 통과 */