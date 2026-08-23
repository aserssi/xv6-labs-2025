#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define DATASIZE (8*4096)

int
match(char *p, char *s)
{
  while(*s){
    if(*p != *s)
      return 0;
    p++;
    s++;
  }
  return 1;
}

int
main(int argc, char *argv[])
{
  char *p = sbrk(DATASIZE);
  char *hint = "This may help.";

  if(p == SBRK_ERROR)
    exit(1);

  for(int i = 0; i < DATASIZE - 32; i++){
    if(match(p + i, hint)){
      printf("%s\n", p + i + 16);
      exit(0);
    }
  }

  exit(1);
}
