/* Tests argument passing to child processes. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("I'm your father");
  exec ("child-args childarg");
}

/* 부모 프로세스가 exec를 호출할 때 넘겨준 명령어와 인자들이 */
/* 자식 프로세스의 main 함수에 정확히 전달되는가? */
/* child-arg.c 파일은 Pintos의 표준 테스트 파일이라고 함 */
/* 자식 프로세스를 child-args를 실행시키고 childarg를 매개변수로 제대로 넘기면 테스트 통과 */