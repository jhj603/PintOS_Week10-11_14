/* Writes out a fairly large file sequentially, one fixed-size
   block at a time, then reads it back to verify that it was
   written properly. */

#define TEST_SIZE 75678
#define BLOCK_SIZE 513
#include "tests/filesys/base/seq-block.inc"

/* 큰 파일에 순차적으로 고정된 크기의 블럭만큼 쓰기 작업을 수행. */
/* 파일을 읽었을 때 적절하게 쓰였는지 검증하면 테스트 통과 */

/* create, open, write, close, filesize, read 시스템 콜이 제대로 구현됐으면 테스트 통과 */
