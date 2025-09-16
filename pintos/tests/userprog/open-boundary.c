/* Creates a file whose name spans the boundary between two pages.
   This is valid, so it must succeed. */

#include <syscall.h>
#include "tests/userprog/boundary.h"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (open (copy_string_across_boundary ("sample.txt")) > 1,
         "open \"sample.txt\"");
}

/* 파일 이름이 페이지 경계에 있을 때 유효성 검사를 하는지 체크 */
/* 파일 열기에 성공해서 2 이상을 반환해야 테스트 성공 */