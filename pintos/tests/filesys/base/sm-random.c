/* Writes out the content of a fairly small file in random order,
   then reads it back in random order to verify that it was
   written properly. */

#define BLOCK_SIZE 13
#define TEST_SIZE (13 * 123)
#include "tests/filesys/base/random.inc"

/* 작은 파일의 내용을 임의의 순서로 쓰고, 다시 임의의 순서로 읽어 데이터가 올바르게 쓰였는지 확인. */
/* seek, write, read 시스템 콜의 기본적인 임의 접근(random access) 기능이 올바르게 동작하는지 검증. */
