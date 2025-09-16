/* Verifies that trying to create a file under a name that
   already exists will fail. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (create ("quux.dat", 0), "create quux.dat");
  CHECK (create ("warble.dat", 0), "create warble.dat");
  CHECK (!create ("quux.dat", 0), "try to re-create quux.dat");
  CHECK (create ("baffle.dat", 0), "create baffle.dat");
  CHECK (!create ("warble.dat", 0), "try to re-create quux.dat");
}

/* 파일을 생성 후 같은 이름으로 재생성 시도 */
/* 이미 존재하는 파일이라면 false를 반환해서 중복 생성 시도 실패 */
/* 중복 생성 시도 실패 처리를 해야 테스트 통과 */