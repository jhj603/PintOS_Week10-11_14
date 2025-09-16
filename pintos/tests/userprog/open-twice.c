/* Tries to open the same file twice,
   which must succeed and must return a different file descriptor
   in each case. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int h1 = open ("sample.txt");
  int h2 = open ("sample.txt");  

  CHECK ((h1 = open ("sample.txt")) > 1, "open \"sample.txt\" once");
  CHECK ((h2 = open ("sample.txt")) > 1, "open \"sample.txt\" again");
  if (h1 == h2)
    fail ("open() returned %d both times", h1);
}

/* 기존에 열었던 파일을 한 번 더 열기 시도 */
/* 각 시도마다 다른 파일 식별자를 반환해야 테스트 성공 */