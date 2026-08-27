# xv6-labs-2025

本仓库用于记录同济大学操作系统课程设计中的 **xv6 及 Labs 课程项目**。项目基于 MIT 6.1810 的 RISC-V 版 xv6，通过修改一个小型但完整的教学操作系统，完成用户程序、系统调用、页表、Trap、写时复制、锁、文件系统、内存映射和网络等实验。

> 本仓库采用“一项实验一个分支”的方式保存代码；`main` 分支用于项目说明与实验导航。

## 完成情况

全部 9 项实验均已完成，并通过对应分支的自动评分测试，总成绩为 **983 / 983**。

| 实验 | 分支 | 主要内容 | 测试成绩 | 状态 |
| --- | --- | --- | ---: | :---: |
| Utilities | [`util`](https://github.com/aserssi/xv6-labs-2025/tree/util) | `sleep`、`sixfive`、`memdump`、`find`、`exec` 等用户程序 | 131 / 131 | ✅ |
| System Calls | [`syscall`](https://github.com/aserssi/xv6-labs-2025/tree/syscall) | 系统调用接口、参数传递、sandbox 与攻击测试 | 45 / 45 | ✅ |
| Page Tables | [`pgtbl`](https://github.com/aserssi/xv6-labs-2025/tree/pgtbl) | `ugetpid`、页表打印与 superpage | 41 / 41 | ✅ |
| Traps | [`traps`](https://github.com/aserssi/xv6-labs-2025/tree/traps) | RISC-V 汇编、backtrace、用户级 alarm | 95 / 95 | ✅ |
| Copy-on-Write | [`cow`](https://github.com/aserssi/xv6-labs-2025/tree/cow) | COW fork、页引用计数与写时复制缺页处理 | 130 / 130 | ✅ |
| Locks | [`lock`](https://github.com/aserssi/xv6-labs-2025/tree/lock) | 并发内存分配、锁竞争优化与读写锁 | 100 / 100 | ✅ |
| File System | [`fs`](https://github.com/aserssi/xv6-labs-2025/tree/fs) | 大文件、双重间接块与符号链接 | 100 / 100 | ✅ |
| mmap | [`mmap`](https://github.com/aserssi/xv6-labs-2025/tree/mmap) | 文件内存映射、延迟分配、`munmap` 与 fork | 170 / 170 | ✅ |
| Networking | [`net`](https://github.com/aserssi/xv6-labs-2025/tree/net) | E1000 网卡驱动、ARP、IP、UDP 与 DNS | 171 / 171 | ✅ |

## 实验环境

- Windows 11 + WSL 2
- Ubuntu
- QEMU RISC-V（`qemu-system-riscv64`）
- RISC-V GNU 交叉编译工具链
- GNU Make、Git、Python 3

可使用以下命令检查主要工具：

```bash
qemu-system-riscv64 --version
riscv64-linux-gnu-gcc --version
make --version
python3 --version
```

## 获取与运行

克隆仓库：

```bash
git clone https://github.com/aserssi/xv6-labs-2025.git
cd xv6-labs-2025
```

切换到需要查看的实验分支，例如：

```bash
git checkout util
```

编译并启动 xv6：

```bash
make clean
make qemu CPUS=1
```

进入 xv6 shell 后即可运行该分支对应的测试程序。退出 QEMU 时按：

```text
Ctrl-a x
```

## 自动测试

在对应实验分支的仓库根目录运行：

```bash
make grade
```

也可以只运行某个测试，例如：

```bash
./grade-lab-util sleep
./grade-lab-pgtbl pgtbltest
./grade-lab-mmap "mmap basic"
```

不同实验的评分脚本名称不同，请在相应分支运行该分支自带的 `grade-lab-*` 脚本。

## 分支说明

```text
main     项目说明与实验导航
util     Unix utilities
syscall  System calls
pgtbl    Page tables
traps    Traps
cow      Copy-on-write
lock     Locks
fs       File system
mmap     Memory-mapped files
net      Networking
```

各实验分支包含该实验对应的 xv6 源码修改、测试脚本以及提交记录。若要比较某项实验相对初始代码的变化，可在 GitHub 中查看该分支的提交历史或文件差异。

## 参考资料

- [MIT 6.1810: Operating System Engineering](https://pdos.csail.mit.edu/6.1810/)
- [xv6 Labs 2025](https://pdos.csail.mit.edu/6.1810/2025/labs/)
- [xv6: a simple, Unix-like teaching operating system](https://pdos.csail.mit.edu/6.1810/2025/xv6/book-riscv-rev5.pdf)
- [MIT xv6-riscv](https://github.com/mit-pdos/xv6-riscv)

## 说明

本仓库用于课程学习、实验复盘与答辩展示。实验实现应结合源码、测试结果和实验报告进行理解，请遵守所在课程的学术诚信要求。
