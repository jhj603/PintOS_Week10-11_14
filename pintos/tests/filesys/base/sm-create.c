/* Tests that create properly zeros out the contents of a fairly
   small file. */

#define TEST_SIZE 5678
#include "tests/filesys/create.inc"

/* 크기가 작은 파일을 생성하고 내용이 모두 0으로 올바르게 초기화되었는지 검사. */
/* create 시스템 콜이 파일 생성 시 내용을 0으로 채우는 기능을 정확히 수행하는지 확인. */

/* create, open, close, filesize, read가 구현되어 있어야 함. */