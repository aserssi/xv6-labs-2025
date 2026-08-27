//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = kexec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}
uint64
mmapfault(uint64 va, int scause)
{
  struct proc *p = myproc();
  struct vma *v = 0;

  // 找到发生缺页的地址所属的 VMA。
  for(int i = 0; i < NVMA; i++){
    if(p->vmas[i].used &&
       va >= p->vmas[i].addr &&
       va < p->vmas[i].addr + p->vmas[i].length){
      v = &p->vmas[i];
      break;
    }
  }

  if(v == 0)
    return 0;
  // 12：取指缺页；13：读缺页；15：写缺页。
  if(scause == 12 && (v->prot & PROT_EXEC) == 0)
    return 0;
  if(scause == 13 && (v->prot & PROT_READ) == 0)
    return 0;
  if(scause == 15 && (v->prot & PROT_WRITE) == 0)
    return 0;

  uint64 pageva = PGROUNDDOWN(va);

  // 如果页面已经存在，说明这是权限错误，不能重新映射。
  pte_t *pte = walk(p->pagetable, pageva, 0);
  if(pte != 0 && (*pte & PTE_V))
    return 0;

  char *mem = kalloc();
  if(mem == 0)
    return 0;

  memset(mem, 0, PGSIZE);

  uint64 fileoff = v->offset + (pageva - v->addr);

  ilock(v->file->ip);
  int n = readi(v->file->ip, 0, (uint64)mem, fileoff, PGSIZE);
  iunlock(v->file->ip);

  if(n < 0){
    kfree(mem);
    return 0;
  }

  int perm = PTE_U;

  if(v->prot & PROT_READ)
    perm |= PTE_R;
  if(v->prot & PROT_WRITE)
    perm |= PTE_R | PTE_W;
  if(v->prot & PROT_EXEC)
    perm |= PTE_X;

  if(mappages(p->pagetable, pageva, PGSIZE,
              (uint64)mem, perm) != 0){
    kfree(mem);
    return 0;
  }

  return (uint64)mem;
}

uint64
sys_mmap(void)
{
  uint64 hint, length, offset;
  int prot, flags, fd;
  struct file *f;
  struct proc *p = myproc();
  int slot = -1;

  argaddr(0, &hint);
  argaddr(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argaddr(5, &offset);

  if(hint != 0 || length == 0)
    return -1;

  if((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
    return -1;

  if(flags != MAP_SHARED && flags != MAP_PRIVATE)
    return -1;

  if(offset % PGSIZE != 0)
    return -1;

  if(argfd(4, &fd, &f) < 0 || f->type != FD_INODE)
    return -1;

  // 文件内容必须可读。
  if(f->readable == 0)
    return -1;

  // 共享可写映射要求文件本身以可写方式打开。
  if((flags == MAP_SHARED) &&
     (prot & PROT_WRITE) &&
     f->writable == 0)
    return -1;

  uint64 addr = PGROUNDUP(p->sz);

  // 找空闲 VMA，并把新映射放在现有映射之后。
  for(int i = 0; i < NVMA; i++){
    if(p->vmas[i].used){
      uint64 end = PGROUNDUP(p->vmas[i].addr +
                             p->vmas[i].length);
      if(end > addr)
        addr = end;
    } else if(slot == -1){
      slot = i;
    }
  }

  if(slot == -1)
    return -1;

  length = PGROUNDUP(length);

  if(addr + length < addr || addr + length >= TRAPFRAME)
    return -1;

  struct vma *v = &p->vmas[slot];
  v->used = 1;
  v->addr = addr;
  v->length = length;
  v->prot = prot;
  v->flags = flags;
  v->offset = offset;
  v->file = filedup(f);

  // 此处不分配物理页，真正访问时再缺页加载。
  return addr;
}

static int
vma_unmap_pages(struct proc *p, struct vma *v,
                uint64 addr, uint64 length)
{
  int failed = 0;
  uint64 end = addr + length;

  for(uint64 a = addr; a < end; a += PGSIZE){
    pte_t *pte = walk(p->pagetable, a, 0);

    // 尚未发生缺页的页面不需要释放。
    if(pte == 0 || (*pte & PTE_V) == 0)
      continue;

    // MAP_SHARED 的可写页面写回原文件。
    if(v->flags == MAP_SHARED &&
       (v->prot & PROT_WRITE)){
      uint64 pa = PTE2PA(*pte);
      uint64 fileoff = v->offset + (a - v->addr);
      struct inode *ip = v->file->ip;

      begin_op();
      ilock(ip);

      if(fileoff < ip->size){
        uint n = ip->size - fileoff;
        if(n > PGSIZE)
          n = PGSIZE;

        if(writei(ip, 0, pa, fileoff, n) != n)
          failed = -1;
      }

      iunlock(ip);
      end_op();
    }

    uvmunmap(p->pagetable, a, 1, 1);
  }

  return failed;
}

uint64
sys_munmap(void)
{
  uint64 addr, length;
  struct proc *p = myproc();

  argaddr(0, &addr);
  argaddr(1, &length);

  if(length == 0 || addr % PGSIZE != 0)
    return -1;

  length = PGROUNDUP(length);

  if(addr + length < addr)
    return -1;

  uint64 end = addr + length;

  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];

    if(!v->used)
      continue;

    uint64 vend = v->addr + v->length;

    // 要解除的范围必须完整位于同一个 VMA 中。
    if(addr < v->addr || end > vend)
      continue;

    // 本实验只要求从头、从尾或整个 VMA 解除映射。
    if(addr != v->addr && end != vend)
      return -1;

    int result = vma_unmap_pages(p, v, addr, length);

    if(addr == v->addr && end == vend){
      fileclose(v->file);
      memset(v, 0, sizeof(*v));
    } else if(addr == v->addr){
      // 删除 VMA 前部。
      v->addr += length;
      v->length -= length;
      v->offset += length;
    } else {
      // 删除 VMA 尾部。
      v->length = addr - v->addr;
    }

    return result < 0 ? -1 : 0;
  }

  return -1;
}

void
vmacleanup(struct proc *p)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];

    if(!v->used)
      continue;

    vma_unmap_pages(p, v, v->addr, v->length);
    fileclose(v->file);
    memset(v, 0, sizeof(*v));
  }
}
