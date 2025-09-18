/* Try reading a file in the most normal way. */

#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  check_file ("sample.txt", sample, sizeof sample - 1);
}

/* 열려있는 파일의 데이터를 읽어와서 쓰면 테스트 통과 */
/* read인데.. write까지 구현해야 테스트 통과.. */
/* filesize까지 구현해야 했다니.. */