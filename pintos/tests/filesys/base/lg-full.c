/* Writes out the contents of a fairly large file all at once,
   and then reads it back to make sure that it was written
   properly. */

#define TEST_SIZE 75678
#include "tests/filesys/base/full.inc"

/* 큰 사이즈의 파일에 데이터를 한 번에 쓰고, 그 내용이 올바르게 저장됐는지 다시 읽어서 확인 */
/* write 시스템 콜이 대용량 쓰기와 파일 확장, 데이터 무결성을 올바르게 처리하는 지 검증하는 테스트 */
/* 쓴 다음 읽었을 때, 전부 올바르게 저장됐으면 테스트 통과 */

/* sys_write 함수와 내부에서 호출되는 file_write, 더 하위 레벨인 inode_write_at 함수가 */
/* 대용량 데이터를 문제없이 처리할 수 있도록 구현되면 테스트 통과 */
/* sys_write를 구현했을 때, write 관련 테스트 통과가 되면 통과될 듯 */

/* create, open, write, close, filesize 구현하면 통과할 듯 */