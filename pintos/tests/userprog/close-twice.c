/* Opens a file and then tries to close it twice.  The second
   close must either fail silently or terminate with exit code
   -1. */

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
  msg ("close \"sample.txt\" again");
  close (handle);
}


/* 한 파일을 열고 두 번 닫으려 시도한다. */
/* 두 번째 닫기 시도에서 조용히 실패하고 종료 코드 -1을 반환 */
/* 첫 닫기 시도는 0, 두 번째는 -1을 반환하면 테스트 통과 */