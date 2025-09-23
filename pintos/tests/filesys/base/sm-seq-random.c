/* Writes out a fairly large file sequentially, one random-sized
   block at a time, then reads it back to verify that it was
   written properly. */

#define TEST_SIZE 5678
#include "tests/filesys/base/seq-random.inc"

/* 작은 파일에 랜덤한 사이즈의 블록 크기만큼 순차적으로 작성 */
/* 적절하게 작성됐는지 읽기 작업으로 검증하면 테스트 통과 */

/* create, open, write, close, filesize, read, seek 시스템 콜이 제대로 구현됐으면 테스트 통과 */