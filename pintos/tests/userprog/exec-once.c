/* Executes and waits for a single child process. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("I'm your father");
  exec ("child-simple");
}

/* 자식은 child-simple 프로그램을 실행시키고 */
/* 부모는 자식을 위해 대기하면 테스트 통과 */