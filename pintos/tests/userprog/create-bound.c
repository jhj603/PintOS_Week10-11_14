/* Opens a file whose name spans the boundary between two pages.
   This is valid, so it must succeed. */

#include <syscall.h>
#include "tests/userprog/boundary.h"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("create(\"quux.dat\"): %d",
       create (copy_string_across_boundary ("quux.dat"), 0));
}

/* 파일 이름 문자열이 페이지 경계에 걸쳐 있는 경우를 테스트 */
/* 페이지 경계에 걸쳐 있을 때 다음 페이지의 유효성도 검사해야 함. */
/* 유효성 검사 처리를 해야 테스트 통과 */