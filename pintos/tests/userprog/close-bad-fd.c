/* Tries to close an invalid fd, which must either fail silently
   or terminate with exit code -1. */

#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  close (0x20101234);
}

/* 파일 닫기 때도 유효성 검사를 수행해야 테스트 통과 */