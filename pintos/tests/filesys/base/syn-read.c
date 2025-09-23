/* Spawns 10 child processes, all of which read from the same
   file and make sure that the contents are what they should
   be. */

#include <random.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "tests/filesys/base/syn-read.h"

static char buf[BUF_SIZE];

#define CHILD_CNT 10

void
test_main (void) 
{
  pid_t children[CHILD_CNT];
  int fd;

  CHECK (create (file_name, sizeof buf), "create \"%s\"", file_name);
  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  random_bytes (buf, sizeof buf);
  CHECK (write (fd, buf, sizeof buf) > 0, "write \"%s\"", file_name);
  msg ("close \"%s\"", file_name);
  close (fd);

  exec_children ("child-syn-read", children, CHILD_CNT);
  wait_children (children, CHILD_CNT);
}

/* 10개의 자식 프로세스를 생성하여 모두 동일한 파일을 동시에 읽음. */
/* 각 프로세스가 파일의 전체 내용을 올바르게 읽을 수 있는지 확인. */
/* 여러 프로세스가 각자의 파일 디스크립터(fd)를 통해 같은 파일을 동시에 읽을 때, */
/* 파일 시스템이 동기화 문제를 일으키지 않고 각 읽기 작업을 올바르게 처리하면 테스트 통과. */

/* 부모 프로세스를 위해 create, open, write, close, exec, wait이 구현되어야 하고 */
/* 자식 프로세스를 위해 open, read, close가 구현되어 있어야 한다. */