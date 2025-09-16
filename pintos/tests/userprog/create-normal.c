/* Creates an ordinary empty file. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (create ("quux.dat", 0), "create quux.dat");
}

/* quux.dat 파일 생성 성공 시 테스트 통과 */