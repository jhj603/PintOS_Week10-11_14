/* Open a file. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle = open ("sample.txt");
  if (handle < 2)
    fail ("open() returned %d", handle);
}

/* 파일 열기 성공 시 테스트 통과 */
/* 반환받는 파일 식별자 값이 2 이상이어야 함. */
/* 0과 1은 표준 입출력 식별자 이므로 */
/* 일반 파일들은 2 이상의 식별자를 사용해야 함.*/
/* -1(실패)나 0, 1을 반환하면 테스트 실패 */