/* Opens a file and then closes it. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle;
  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");
  msg ("close \"sample.txt\"");
  close (handle);
}

/* 열린 파일을 정상적으로 닫으면 테스트 통과 */