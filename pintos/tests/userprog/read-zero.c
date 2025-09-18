/* Try a 0-byte read, which should return 0 without reading
   anything. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle, byte_cnt;
  char buf;

  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");

  buf = 123;
  byte_cnt = read (handle, &buf, 0);
  if (byte_cnt != 0)
    fail ("read() returned %d instead of 0", byte_cnt);
  else if (buf != 123)
    fail ("0-byte read() modified buffer");
}

/* 0 바이트를 읽어오려 시도함. */
/* 아무거나 읽어오지 않고 0 반환해야 테스트 통과 */