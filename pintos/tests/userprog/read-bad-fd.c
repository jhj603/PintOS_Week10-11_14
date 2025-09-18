/* Tries to read from an invalid fd,
   which must either fail silently or terminate the process with
   exit code -1. */

#include <limits.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  char buf;
  read (0x20101234, &buf, 1);
  read (5, &buf, 1);
  read (1234, &buf, 1);
  read (-1, &buf, 1);
  read (-1024, &buf, 1);
  read (INT_MIN, &buf, 1);
  read (INT_MAX, &buf, 1);
}

/* 유효하지 않은 파일 디스크립터로 read 시도 시 */
/* 종료 코드 -1을 반환하고 조용히 실패하던가 프로세스 종료 */
/* 실패하면서 -1 반환해야 테스트 통과 */

/* 매우 큰 양수 값으로 일반적으로 파일 디스크립터 테이블 크기를 훨씬 벗어남 */
/* 어떤 파일도 open하지 않았기 때문에 5라는 식별자는 유효하지 않음 */
/* 파일 디스크립터 테이블의 최대 크기를 초과할 가능성이 높은 값들 */
/* 파일 디스크립터는 음수를 가질 수 없어 유효하지 않음 */