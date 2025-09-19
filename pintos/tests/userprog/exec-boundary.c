/* Forks a thread whose name spans the boundary between two pages.
   This is valid, so it must succeed. */

#include <syscall.h>
#include "tests/userprog/boundary.h"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  pid_t pid = fork ("child-simple");
  if (pid == 0){
    exec (copy_string_across_boundary ("child-simple"));
  } else {
    int exit_val = wait(pid);
    CHECK (pid > 0, "fork");
    CHECK (exit_val == 81, "wait");
  }
}

/* 두 페이지 경계에 걸친 이름을 넘겼을 때 유효성 검사를 수행하고 */
/* 통과해서 제대로 수행되면 테스트 통과 */