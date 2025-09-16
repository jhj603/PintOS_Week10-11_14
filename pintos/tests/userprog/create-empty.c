/* Tries to create a file with the empty string as its name. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("create(\"\"): %d", create ("", 0));
}

/* 파일 이름이 비어있는 경우 파일 생성 실패해야 함. */
/* 파일 생성 실패 시 테스트 통과 */