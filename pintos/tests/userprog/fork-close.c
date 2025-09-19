/* After fork, the child process will close the opened file
   and the parent will access the closed file. */

#include <string.h>
#include <syscall.h>
#include "tests/userprog/boundary.h"
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  pid_t pid;
  int handle;
  int byte_cnt;
  char *buffer;

  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");
  
  if ((pid = fork("child"))){
    wait (pid);

    buffer = get_boundary_area () - sizeof sample / 2;
    byte_cnt = read (handle, buffer, sizeof sample - 1);
    if (byte_cnt != sizeof sample - 1)
      fail ("read() returned %d instead of %zu", byte_cnt, sizeof sample - 1);
    else if (strcmp (sample, buffer)) {
        msg ("expected text:\n%s", sample);
        msg ("text actually read:\n%s", buffer);
        fail ("expected text differs from actual");
    } else {
      msg ("Parent success");
    }
    
    close(handle);
  } else {
    msg ("child run");
    close(handle);
  }
}

/* 포크 후, 자식 프로세스가 열린 파일을 닫는데 부모 프로세스가 그 파일을 */
/* 접근하려 함. 성공적으로 읽을 수 있으면 테스트 통과 */
/* 부모의 fd_table에 있는 각 파일에 대해 file_duplicate()를 호출해 새로운 열린 파일 객체를 만들어 */
/* fd_table에 복사해야 함. */