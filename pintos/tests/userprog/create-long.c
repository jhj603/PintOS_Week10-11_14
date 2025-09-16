/* Tries to create a file with a name that is much too long,
   which must fail. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  static char name[512];
  memset (name, 'x', sizeof name);
  name[sizeof name - 1] = '\0';
  
  msg ("create(\"x...\"): %d", create (name, 0));
}

/* create 함수는 성공 시 1, 실패 시 0을 반환 */
/* 너무 긴 이름의 문자열이 입력됐을 때, 파일 생성 실패 */
/* 0을 반환해서 파일 생성을 하지 못해야 테스트 성공 */