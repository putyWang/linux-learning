### Linux 0.11版本注释学习笔记

#### 目录结构与备注

```
📁root 
├─ 📄Makefile ## 根目录编译批处理文件
├─ 📁init ## 内核初始化程序
│  └─ 📄main.c
├─ 📁tools ## 生成内核 image 文件的工具程序
│  └─ 📄build.c ## 将各目录编译生成的代码连接成一个完整的进程内核文件 image
├─ 📁boot ## 系统引导汇编程序
│  ├─ 📄head.s ## 编译链接在内核模块最前面，用来对硬件设备进行探测以及内存管理页的初始化设置；
│  ├─ 📄setup.s ## 读取机器硬件配置参数的同时将内核模块移动至内存合适位置；
│  └─ 📄bootsect.s ## 磁盘引导块程序，编译后驻留在磁盘第一个扇区中；
├─ 📁lib ## 内核库函数，为初始化程序 init/main.c 执行在用户态进程提供调用支持
│  ├─ 📄execve.c
│  ├─ 📄ctype.c
│  ├─ 📄Makefile
│  ├─ 📄errno.c
│  ├─ 📄string.c
│  ├─ 📄_exit.c
│  ├─ 📄setsid.c
│  ├─ 📄malloc.c
│  ├─ 📄wait.c
│  ├─ 📄write.c
│  ├─ 📄dup.c
│  ├─ 📄open.c
│  └─ 📄close.c
├─ 📁mm ## 内存管理程序 管理程序对主内存区的使用
│  ├─ 📄page.s ## 包含内存页面异常中断处理程序
│  ├─ 📄memory.c ## 内存初始化及缺页异常时调用的函数
│  └─ 📄Makefile
├─ 📁fs ## 文件系统
│  ├─ 📄super.c ## 文件系统超级块的处理函数
│  ├─ 📄file_table.c
│  ├─ 📄buffer.c ## 高速缓冲区管理程序（所有对文件的访问与操作都会汇集在此程序中，底层调用块设备相关函数）
│  ├─ 📄pipe.c ## 管道读写函数以及创建的系统调用函数；
│  ├─ 📄exec.c ## 拥有所有 exec() 函数族的主要函数 do_exec()；
│  ├─ 📄bitmap.c ## 用于处理文件系统中 i 节点和逻辑数据块的位图；
│  ├─ 📄Makefile
│  ├─ 📄file_dev.c ## 基于 i 节点和描述符结构的文件读写函数；
│  ├─ 📄inode.c ## 对文件系统 i 节点操作的函数；
│  ├─ 📄truncate.c ## 删除文件时释放文件所占用的设备数据空间；
│  ├─ 📄ioctl.c ## 字符设备的 io 控制功能；
│  ├─ 📄namei.c ## 目录以及文件名操作以及系统调用函数；9
│  ├─ 📄block_dev.c ## 程序包含块数据读及写函数；
│  ├─ 📄read_write.c ## 文件读、写及定位三个系统调用函数；
│  ├─ 📄stat.c ## 两个获取文件状态的系统调用
│  ├─ 📄fcntl.c ## 实现文件 I/O 控制的系统函数调用；
│  ├─ 📄open.c ## 实现修改文件属性、创建和关闭文件的系统调用函数；
│  └─ 📄char_dev.c ## 实现字符设备的读写函数；
├─ 📁include ## 头文件
│  ├─ 📄time.h
│  ├─ 📄utime.h
│  ├─ 📄unistd.h
│  ├─ 📄stddef.h
│  ├─ 📄fcntl.h
│  ├─ 📄signal.h
│  ├─ 📄ctype.h
│  ├─ 📄errno.h
│  ├─ 📄termios.h
│  ├─ 📄a.out.h
│  ├─ 📄stdarg.h
│  ├─ 📄const.h
│  ├─ 📄string.h
│  ├─ 📁asm ## 与计算机硬件体系结构密切相关的头文件
│  │  ├─ 📄segment.h
│  │  ├─ 📄io.h
│  │  ├─ 📄memory.h
│  │  └─ 📄system.h
│  ├─ 📁sys ## 与文件状态、进程、系统数据类型有关的头文件
│  │  ├─ 📄types.h
│  │  ├─ 📄times.h
│  │  ├─ 📄wait.h
│  │  ├─ 📄stat.h
│  │  └─ 📄utsname.h
│  └─ 📁linux ## 内核专用部分头文件
│     ├─ 📄head.h
│     ├─ 📄tty.h
│     ├─ 📄config.h
│     ├─ 📄mm.h
│     ├─ 📄hdreg.h
│     ├─ 📄fdreg.h
│     ├─ 📄kernel.h
│     ├─ 📄sys.h
│     ├─ 📄sched.h
│     └─ 📄fs.h
└─ 📁kernel ## 内核进程调度、信号处理、系统调用等部分处理
   ├─ 📄system_call.s ## 实现了系统调用的接口处理过程，实际过程则是其他函数中实现；
   ├─ 📄sched.c ## 包含了进程调度相关函数及一些简单的系统调用函数
   ├─ 📄sys.c ## 包括很多系统调用函数，其中有些还未实现
   ├─ 📄mktime.c ## 包含一个用于计算开机时间的时间函数 mktime()；
   ├─ 📄fork.c ## 进程创建系统调用 sys_fork() 两个函数
   ├─ 📄traps.c ## 对硬件异常的实际处理流程
   ├─ 📄Makefile
   ├─ 📄printk.c ## 包含一个内核专用信息显示函数 printk()
   ├─ 📄exit.c ## 处理系统用于进程中止的系统调用
   ├─ 📄panic.c ## 包含一个用于显示内核出错信息并停机的函数 panic()
   ├─ 📄vsprintf.c ## 实现了以归入标准库中的格式化函数
   ├─ 📄asm.s ## 用于处理系统中由于系统硬件异常所引起的中断，实际调用 traps.c 中对应函数进行处理
   ├─ 📄signal.c ## 包含了有关信号处理的4个系统调用及一个在对应中断处理程序中处理信号的函数 do_signal()
   ├─ 📁math ## 数学协处理器仿真处理程序
   │  ├─ 📄Makefile
   │  └─ 📄math_emulate.c 
   ├─ 📁chr_drv ## 字符设备驱动程序
   │  ├─ 📄serial.c ## 异步串行通信芯片 UART 进行初始化，同时设置两个端口的中断向量
   │  ├─ 📄tty_ioctl.c ## 实现了 tty 中的 io 控制接口以及对 termio[s]终端io结构的读写函数
   │  ├─ 📄Makefile
   │  ├─ 📄rs_io.s ## 用于实现两个串行端口的中断处理程序
   │  ├─ 📄console.c ## 控制台初始化程序及控制台写函数，用于被 tty_io 程序调用
   │  ├─ 📄keyboard.S ## 实现了键盘中断处理过程
   │  └─ 📄tty_io.c ## 实现了对 tty 设备的读写函数，为文件系统提供上层访问接口
   └─ 📁blk_drv ## 块设备驱动程序
      ├─ 📄blk.h ## 块设备程序专用头文件
      ├─ 📄ramdisk.c ## 实现对内存虚拟设备进行读写
      ├─ 📄Makefile
      ├─ 📄ll_rw_blk.c ## 实现低层块设备的读写函数（内核中所有程序都是通过这个程序对块进行访问的）
      ├─ 📄floppy.c ## 实现对软盘数据块的读写底层驱动函数
      └─ 📄hd.c ## 主要实现对硬盘数据块进行读写的底层驱动函数
```

#### 内核使用寄存器

- **ss**：堆栈段寄存器（堆栈底部指针地址）；
- **esp**：堆栈指针寄存器（堆栈顶部位置地址）；
- 

