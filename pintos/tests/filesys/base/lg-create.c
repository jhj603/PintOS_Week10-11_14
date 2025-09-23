/* Tests that create properly zeros out the contents of a fairly
   large file. */

#define TEST_SIZE 75678
#include "tests/filesys/create.inc"

/* 비교적 큰 파일을 생성하고 그 파일의 모든 내용이 0으로 올바르게 초기화됐는지 검사 */
/* TEST_SIZE를 75678 바이트로 정의하고 create 시스템 콜을 사용해 75678 크기의 파일 생성 */
/* 만든 파일을 open 시스템 콜로 열고 read 시스템 콜로 파일의 처음부터 끝까지 모두 읽어 메모리로 가져옴 */
/* 가져온 데이터들이 모두 0인지 한 바이트씩 확인하고 모두 0이라면 테스트 통과 */

/* sys_create에서 받아온 크기를 filesys_create로 잘 넘기면 filesys_create 내부에서 */
/* 올바르게 처리됨. 따라서 filesys_create로 잘 넘기기만 하면 통과 */

/* create, open, close, filesize, read가 구현되어 있어야 함. */