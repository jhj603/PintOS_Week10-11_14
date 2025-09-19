/* Tries to execute a nonexistent process.
   The exec system call must return -1. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("exec(\"no-such-file\"): %d", exec ("no-such-file"));
}

/* 존재하지 않는 프로그램을 실행시키려 할 때, */
/* -1을 반환하면 테스트 통과 */