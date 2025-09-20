/* Ensure that the executable of a running process cannot be
   modified, even by a child process. */

#define CHILD_CNT "1"
#include "tests/userprog/rox-child.inc"

/* rox는 읽기 전용 실행 파일을 의미함 */
/* 프로세스가 실행을 시작하면 그 프로세스의 원본 실행 파일은 수정될 수 없다. */
/* 자식 프로세스에 의해서도 수정될 수 없어야 테스트 통과 */