/* Writes out the contents of a fairly small file all at once,
   and then reads it back to make sure that it was written
   properly. */

#define TEST_SIZE 5678
#include "tests/filesys/base/full.inc"

/* 작은 파일에 데이터를 한 번에 쓰고, 다시 읽어 내용이 올바르게 저장되었는지 확인. */
/* 단일 쓰기 요청을 통해 파일 크기를 확장하고 데이터를 정확히 기록하는 기본 기능을 검증. */
/* create, open, write, read, filesize, close 시스템 콜을 구현하면 테스트 통과. */