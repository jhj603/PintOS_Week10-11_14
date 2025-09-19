/* Passes an invalid pointer to the exec system call.
   The process must be terminated with -1 exit code. */

#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  exec ((char *) 0x20101234);
}

/* exec 시스템 콜에 유효하지 않은 주소를 넘겼을 때 */
/* 종료 코드 -1을 반환하면서 프로세스 종료되면 테스트 통과 */