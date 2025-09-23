/* Writes out the content of a fairly large file in random order,
   then reads it back in random order to verify that it was
   written properly. */

#define BLOCK_SIZE 512
#define TEST_SIZE (512 * 150)
#include "tests/filesys/base/random.inc"

/* 랜덤한 순서로 큰 파일을 만들고 데이터를 작성. 역순으로 읽는데 */
/* 원래 파일처럼 제대로 작성됐으면 테스트 통과 */

/* create, open, write, close, filesize, read 시스템 콜이 제대로 구현됐으면 테스트 통과 */