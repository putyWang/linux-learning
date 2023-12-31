#ifndef _CONST_H
#define _CONST_H

#define BUFFER_END 0x200000 // 缓冲使用的内存的末端（代码中未使用）

#define I_TYPE          0170000 // 指明 i 节点的类型
#define I_DIRECTORY	0040000 // 是目录文件
#define I_REGULAR       0100000 // 非目录文件或特殊文件的常规文件
#define I_BLOCK_SPECIAL 0060000 // 块设备特殊文件
#define I_CHAR_SPECIAL  0020000 // 字符设备特殊文件
#define I_NAMED_PIPE	0010000 // 命名管道
#define I_SET_UID_BIT   0004000 // 在执行时设置有效用户 id 类型
#define I_SET_GID_BIT   0002000 // 在执行时设置有效组 id 类型

#endif
