/* Recursively forks until the child fails to fork.
   We expect that at least 28 copies can run.
   
   We count how many children your kernel was able to execute
   before it fails to start a new process.  We require that,
   if a process doesn't actually get to start, exec() must
   return -1, not a valid PID.

   We repeat this process 10 times, checking that your kernel
   allows for the same level of depth every time.

   In addition, some processes will spawn children that terminate
   abnormally after allocating some resources.

   We set EXPECTED_DEPTH_TO_PASS heuristically by
   giving *large* margin on the value from our implementation.
   If you seriously think there is no memory leak in your code
   but it fails with EXPECTED_DEPTH_TO_PASS,
   please manipulate it and report us the actual output.
   
   Orignally written by Godmar Back <godmar@gmail.com>
   Modified by Minkyu Jung, Jinyoung Oh <cs330_ta@casys.kaist.ac.kr>
*/

#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syscall.h>
#include <random.h>
#include "tests/lib.h"

static const int EXPECTED_DEPTH_TO_PASS = 10;
static const int EXPECTED_REPETITIONS = 10;

int make_children (void);

/* Open a number of files (and fail to close them).
   The kernel must free any kernel resources associated
   with these file descriptors. */
static void
consume_some_resources (void)
{
  int fd, fdmax = 126;

  /* Open as many files as we can, up to fdmax.
	 Depending on how file descriptors are allocated inside
	 the kernel, open() may fail if the kernel is low on memory.
	 A low-memory condition in open() should not lead to the
	 termination of the process.  */
  for (fd = 0; fd < fdmax; fd++) {
#ifdef EXTRA2
	  if (fd != 0 && (random_ulong () & 1)) {
		if (dup2(random_ulong () % fd, fd+fdmax) == -1)
			break;
		else
			if (open (test_name) == -1)
			  break;
	  }
#else
		if (open (test_name) == -1)
		  break;
#endif
  }
}

/* Consume some resources, then terminate this process
   in some abnormal way.  */
static int NO_INLINE
consume_some_resources_and_die (void)
{
  consume_some_resources ();
  int *KERN_BASE = (int *)0x8004000000;

  switch (random_ulong () % 5) {
	case 0:
	  *(int *) NULL = 42;
    break;

	case 1:
	  return *(int *) NULL;

	case 2:
	  return *KERN_BASE;

	case 3:
	  *KERN_BASE = 42;
    break;

	case 4:
	  open ((char *)KERN_BASE);
	  exit (-1);
    break;

	default:
	  NOT_REACHED ();
  }
  return 0;
}

int
make_children (void) {
  int i = 0;
  int pid;
  char child_name[128];
  for (; ; random_init (i), i++) {
    if (i > EXPECTED_DEPTH_TO_PASS/2) {
      snprintf (child_name, sizeof child_name, "%s_%d_%s", "child", i, "X");
      pid = fork(child_name);
      if (pid > 0 && wait (pid) != -1) {
        fail ("crashed child should return -1.");
      } else if (pid == 0) {
        consume_some_resources_and_die();
        fail ("Unreachable");
      }
    }

    snprintf (child_name, sizeof child_name, "%s_%d_%s", "child", i, "O");
    pid = fork(child_name);
    if (pid < 0) {
      exit (i);
    } else if (pid == 0) {
      consume_some_resources();
    } else {
      break;
    }
  }

  int depth = wait (pid);
  if (depth < 0)
	  fail ("Should return > 0.");

  if (i == 0)
	  return depth;
  else
	  exit (depth);
}

int
main (int argc UNUSED, char *argv[] UNUSED) {
  test_name = "multi-oom";

  msg ("begin");

  int first_run_depth = make_children ();
  CHECK (first_run_depth >= EXPECTED_DEPTH_TO_PASS, "Spawned at least %d children.", EXPECTED_DEPTH_TO_PASS);

  for (int i = 0; i < EXPECTED_REPETITIONS; i++) {
    int current_run_depth = make_children();
    if (current_run_depth < first_run_depth) {
      fail ("should have forked at least %d times, but %d times forked", 
              first_run_depth, current_run_depth);
    }
  }

  msg ("success. Program forked %d iterations.", EXPECTED_REPETITIONS);
  msg ("end");
}

/* 재귀적으로 fork를 수행하는데 자식들이 fork를 실패할 때까지 수행한다. */
/* 적어도 28개의 자식이 동작할 수 있는 것을 기대한다. */
/* 새로운 프로세스를 시작하는데 실패하기 전까지 얼마나 많은 자식들이 실행될 수 있는지 센다. */
/* 프로세스가 제대로 시작할 수 없을 때 exec는 유효한 pid가 아닌 -1을 반환해야 한다. */
/* 이 프로세스를 10번 반복해서 커널이 같은 깊이의 동작을 매번 수행할 수 있는지 검사한다. */

/* 메모리 부족 상황과 비정상적인 프로세스 종료 상황에서 */
/* 커널이 얼마나 안정적으로 동작하고 리소스를 완벽하게 정리(메모리 누수 방지)하는지 검증 */

/* process_exit에서 완벽한 리소스 정리를 처리해야 함. 할당받은 모든 동적 메모리(fd_table)와 열려있는 모든 파일(file_close)을 반드시 해제해야 함 */
/* process_fork(__do_fork) 실행 중 메모리 부족 등으로 자원 할당에 실패하면 그때까지 할당했던 모든 자원을 깨끗하게 해제하고 에러 반환해야 함 */
/* exception.c에서 유저 프로세스가 페이지 폴트와 같은 예외를 일으켰을 때, 해당 스레드의 exit_status를 -1로 설정한 뒤 thread_exit()을 호출해 */
/* 프로세스를 종료시켜야 함 */