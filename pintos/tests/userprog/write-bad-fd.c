/* Tries to write to an invalid fd,
   which must either fail silently or terminate the process with
   exit code -1. */

#include <limits.h>
#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  char buf = 123;
  write (0x01012342, &buf, 1);
  write (7, &buf, 1);
  write (2546, &buf, 1);
  write (-5, &buf, 1);
  write (-8192, &buf, 1);
  write (INT_MIN + 1, &buf, 1);
  write (INT_MAX - 1, &buf, 1);
}

/* 유효하지 않은 fd로 write 시도. fd의 유효성 검사 필요 */
/* 종료 코드 -1을 반환하면서 실패하거나 프로세스 종료해야 테스트 통과 */

/* 파일 디스크립터의 최대 범위를 넘는 fd 값 */
/* 열리지 않은 파일을 가리키는 fd */
/* 음수 값을 갖는 fd */