/* Executes itself recursively to the depth indicated by the
   first command-line argument. */

#include <debug.h>
#include <stdlib.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"

int
main (int argc UNUSED, char *argv[]) 
{
  test_name = "multi-recurse";

  int n = atoi (argv[1]);

  msg ("begin %d", n);
  if (n != 0) 
    {
      char child_cmd[128];
      pid_t child_pid;
      int code;
      
      snprintf (child_cmd, sizeof child_cmd, "multi-recurse %d", n - 1);
      msg ("exec(\"%s\")", child_cmd);
      if (!(child_pid = fork ("multi-recurse"))){
        exec (child_cmd);
      }
      if (child_pid < 0)
        fail ("fork() returned %d", child_pid);

      code = wait (child_pid);
      if (code != n - 1)
        fail ("wait(exec(\"%s\")) returned %d", child_cmd, code);
    }
  
  msg ("end %d", n);
  return n;
}

/* 재귀적으로 여러 자식 프로세스를 만든다. 만든 프로세스들이 */
/* 생성 순서의 역순으로 종료되고 부모는 자식의 종료 코드가 정상적인지 검증하면 테스트 통과 */