#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

char *seps = " -\r\t\n./,";

void
checknum(int valid, int hasnum, int num)
{
  if(valid && hasnum && (num % 5 == 0 || num % 6 == 0)){
    printf("%d\n", num);
  }
}

void
sixfive(int fd)
{
  char c;
  int n;
  int num = 0;
  int hasnum = 0;
  int valid = 1;

  while((n = read(fd, &c, 1)) > 0){
    if(strchr(seps, c)){
      checknum(valid, hasnum, num);
      num = 0;
      hasnum = 0;
      valid = 1;
    } else if(c >= '0' && c <= '9'){
      if(valid){
        num = num * 10 + (c - '0');
        hasnum = 1;
      }
    } else {
      valid = 0;
    }
  }

  checknum(valid, hasnum, num);
}

int
main(int argc, char *argv[])
{
  int fd;

  if(argc < 2){
    fprintf(2, "usage: sixfive file...\n");
    exit(1);
  }

  for(int i = 1; i < argc; i++){
    fd = open(argv[i], 0);
    if(fd < 0){
      fprintf(2, "sixfive: cannot open %s\n", argv[i]);
      exit(1);
    }

    sixfive(fd);
    close(fd);
  }

  exit(0);
}
