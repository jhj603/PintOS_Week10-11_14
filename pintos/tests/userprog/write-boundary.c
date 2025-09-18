/* Writes data spanning two pages in virtual address space,
   which must succeed. */

#include <string.h>
#include <syscall.h>
#include "tests/userprog/boundary.h"
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle;
  int byte_cnt;
  char *sample_p;

  sample_p = copy_string_across_boundary (sample);

  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");

  byte_cnt = write (handle, sample_p, sizeof sample - 1);
  if (byte_cnt != sizeof sample - 1)
    fail ("write() returned %d instead of %zu", byte_cnt, sizeof sample - 1);
}

/* buffer가 두 페이지에 걸쳐있어도 파일에 쓰기 작업이 성공적으로 수행되어야 함. */
/* 버퍼의 모든 영역이 유효한지 검사하고 쓰기 작업이 수행되면 테스트 통과 */