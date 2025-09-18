/* Try reading from fd 1 (stdout), 
   which may just fail or terminate the process with -1 exit
   code. */

#include <stdio.h>
#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  char buf;
  read (STDOUT_FILENO, &buf, 1);
}

/* 표준 출력에서 데이터를 읽어올 수 없으므로 종료 코드 -1과 함께 */
/* 실패하거나 프로세스 종료하면 테스트 통과 */