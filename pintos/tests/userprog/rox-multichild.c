/* Ensure that the executable of a running process cannot be
   modified, even in the presence of multiple children. */

#define CHILD_CNT "5"
#include "tests/userprog/rox-child.inc"

/* 실행 중인 프로세스의 원본 실행 파일은 수정될 수 없다. */
/* 여러 자식 프로세스에 의해서도 수정되지 않으면 테스트 통과 */