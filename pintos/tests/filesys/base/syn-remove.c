/* Verifies that a deleted file may still be written to and read
   from. */

#include <random.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

char buf1[1234];
char buf2[1234];

void
test_main (void) 
{
  const char *file_name = "deleteme";
  int fd;
  
  CHECK (create (file_name, sizeof buf1), "create \"%s\"", file_name);
  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  CHECK (remove (file_name), "remove \"%s\"", file_name);
  random_bytes (buf1, sizeof buf1);
  CHECK (write (fd, buf1, sizeof buf1) > 0, "write \"%s\"", file_name);
  msg ("seek \"%s\" to 0", file_name);
  seek (fd, 0);
  CHECK (read (fd, buf2, sizeof buf2) > 0, "read \"%s\"", file_name);
  compare_bytes (buf2, buf1, sizeof buf1, 0, file_name);
  msg ("close \"%s\"", file_name);
  close (fd);
}

/* 파일이 열려 있는 상태에서 삭제됐을 때 어떻게 동작하는지 검증 */
/* 파일 이름(디렉토리 엔트리)와 실제 데이터(아이노드)가 분리돼 관리되는지 확인 */
/* remove는 파일의 이름을 디렉토리에서 지우는 역할을 하지만 실제 데이터(inode와 데이터 블록)는 즉시 삭제되지 않을 수 있음 */
/* 파일 데이터는 해당 파일을 가리키는 링크가 하나도 없을 때 영구적으로 삭제 */
/* remove는 디렉토리에 있는 파일 이름인 하드 링크를 제거하는 역할 */
/* 메모리 내 링크는 프로세스가 open을 통해 얻은 파일 디스크립터로 fd가 유효한 동안, 커널은 해당 파일의 inode를 계속 참조 */

/* syn-remove 테스트는 remove를 호출해서 하드 링크를 제거 */
/* 하지만 메모리 내 링크는 계속 남아있으므로 해당 파일에 write, read, seek 등과 같은 파일 연산이 정상적으로 수행되어야 함. */
/* close(fd)가 호출되어야 메모리 내 링크까지 삭제해 파일의 inode와 데이터를 완전히 해제하고 회수 */
