/* Spawns several child processes to write out different parts of
   the contents of a file and waits for them to finish.  Then
   reads back the file and verifies its contents. */

#include <random.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "tests/filesys/base/syn-write.h"
#include "tests/lib.h"
#include "tests/main.h"

char buf1[BUF_SIZE];
char buf2[BUF_SIZE];

void
test_main (void) 
{
  pid_t children[CHILD_CNT];
  int fd;

  CHECK (create (file_name, sizeof buf1), "create \"%s\"", file_name);

  exec_children ("child-syn-wrt", children, CHILD_CNT);
  wait_children (children, CHILD_CNT);

  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  CHECK (read (fd, buf1, sizeof buf1) > 0, "read \"%s\"", file_name);
  random_bytes (buf2, sizeof buf2);
  compare_bytes (buf1, buf2, sizeof buf1, 0, file_name);
}

/* 여러 자식 프로세스를 생성하여 모두 동일한 파일에 각각 파일의 다른 구역에 내용을 씀. */
/* 각 프로세스가 파일의 맡은 내용을 올바르게 써서 파일의 전체 내용이 올바른지 확인. */
/* 여러 프로세스가 각자의 파일 디스크립터(fd)를 통해 같은 파일을 동시에 쓸 때, */
/* 파일 시스템이 동기화 문제를 일으키지 않고 각 쓰기 작업을 올바르게 처리하면 테스트 통과. */