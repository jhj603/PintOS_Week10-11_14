/* Opens a file and then runs a subprocess that tries to close
   the file.  (Pintos does not have inheritance of file handles,
   so this must fail.)  The parent process then attempts to use
   the file handle, which must succeed. */

#include <stdio.h>
#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  char child_cmd[128];
  int handle;

  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");

  snprintf (child_cmd, sizeof child_cmd, "child-close %d", handle);
  
  pid_t pid;
  if (!(pid = fork("child-close"))){
    exec (child_cmd);
  }
  msg ("wait(exec()) = %d", wait (pid));

  check_file_handle (handle, "sample.txt", sample, sizeof sample - 1);
}

/* 파일을 열고 자식 프로세스를 실행시키는 테스트. */
/* 자식은 부모로부터 파일 디스크립터를 상속받고, 상속받은 파일을 닫는다. */
/* 부모는 자식이 종료된 후에도 자신의 파일 디스크립터가 여전히 유효한지 확인한다. */
/* 이를 통해 fork 시 파일 디스크립터가 올바르게 복제(duplicate)되고, 서로 독립적으로 동작하는지 검증한다. */