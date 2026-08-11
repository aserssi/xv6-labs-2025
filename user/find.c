#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  memmove(buf, p, strlen(p));
  buf[strlen(p)] = 0;
  return buf;
}

void
find(char *path, char *name, int execflag, char **cmd, int cmdargc)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0)
    return;

  if(fstat(fd, &st) < 0){
    close(fd);
    return;
  }

  if(st.type == T_FILE){
    if(strcmp(fmtname(path), name) == 0){
      if(execflag){
        char *args[16];
        for(int i = 0; i < cmdargc; i++)
          args[i] = cmd[i];
        args[cmdargc] = path;
        args[cmdargc + 1] = 0;

        if(fork() == 0){
          exec(cmd[0], args);
          exit(1);
        }
        wait(0);
      } else {
        printf("%s\n", path);
      }
    }
    close(fd);
    return;
  }

  if(st.type == T_DIR){
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      close(fd);
      return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;

      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;

      if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;

      find(buf, name, execflag, cmd, cmdargc);
    }
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc < 3){
    fprintf(2, "usage: find path name [-exec cmd args...]\n");
    exit(1);
  }

  int execflag = 0;
  int cmdstart = 0;

  for(int i = 3; i < argc; i++){
    if(strcmp(argv[i], "-exec") == 0){
      execflag = 1;
      cmdstart = i + 1;
      break;
    }
  }

  if(execflag){
    if(cmdstart >= argc){
      fprintf(2, "find: missing exec command\n");
      exit(1);
    }
    find(argv[1], argv[2], 1, &argv[cmdstart], argc - cmdstart);
  } else {
    find(argv[1], argv[2], 0, 0, 0);
  }

  exit(0);
}
