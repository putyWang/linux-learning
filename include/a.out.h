#ifndef _A_OUT_H
#define _A_OUT_H

#define __GNU_EXEC_MACROS__

/**
 * 可执行文件头数据结构
*/
struct exec {
  unsigned long a_magic;	/* Use macros N_MAGIC, etc for access */ // 可执行文件模数，使用 N_MAGIC 等宏访问
  unsigned a_text;		/* length of text, in bytes */ // 代码长度，字节数
  unsigned a_data;		/* length of data, in bytes */ // 数据长度，字节数
  unsigned a_bss;		/* length of uninitialized data area for file, in bytes */ // 文件中未初始化数据区（bss 段）长度，字节数
  unsigned a_syms;		/* length of symbol table data in file, in bytes */ // 文件中符号表长度，字节数
  unsigned a_entry;		/* start address */ // 执行开始地址
  unsigned a_trsize;		/* length of relocation info for text, in bytes */ // 代码重定位信息长度，字节数
  unsigned a_drsize;		/* length of relocation info for data, in bytes */ // 数据重定向信息长度，字节数
};

// 用于取执行结构中的模数
#ifndef N_MAGIC
#define N_MAGIC(exec) ((exec).a_magic)
#endif

#ifndef OMAGIC
/* Code indicating object file or impure executable.  */
#define OMAGIC 0407 // 目标文件或不纯的可执行文件代号（代码和数据段紧跟着执行头后且是连续存放的，内核将代码与数据都加入可读写内存中）
/* Code indicating pure executable.  */
#define NMAGIC 0410 // 指明为纯可执行文件的代号（代码和数据段紧跟着执行头后且是连续存放的，内核将代码写到只读内存中码，将数据加入紧随其后的可读写内存中）
/* Code indicating demand-paged executable.  */
#define ZMAGIC 0413 // 指明为需求分页处理的可执行文件（内核在必要时才会从二进制可执行文件中加载独立的页面，头、代码与数据都被链接程序处理成多个页面大小的块、代码页面只读，数据段页面则是可读写的）
#endif /* not OMAGIC */

// 如果模数不能被识别，则返回真
#ifndef N_BADMAG
#define N_BADMAG(x)					\
 (N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC		\
  && N_MAGIC(x) != ZMAGIC)
#endif

#define _N_BADMAG(x)					\
 (N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC		\
  && N_MAGIC(x) != ZMAGIC)

//程序头尾部内存中的偏移位置
#define _N_HDROFF(x) (SEGMENT_SIZE - sizeof (struct exec))
// 代码起始偏移值
#ifndef N_TXTOFF
#define N_TXTOFF(x) \
 (N_MAGIC(x) == ZMAGIC ? _N_HDROFF((x)) + sizeof (struct exec) : sizeof (struct exec))
#endif

// 数据起始偏移值
#ifndef N_DATOFF
#define N_DATOFF(x) (N_TXTOFF(x) + (x).a_text)
#endif

// 代码重定位信息偏移值 
#ifndef N_TRELOFF
#define N_TRELOFF(x) (N_DATOFF(x) + (x).a_data)
#endif

// 数据重定位信息偏移值
#ifndef N_DRELOFF
#define N_DRELOFF(x) (N_TRELOFF(x) + (x).a_trsize)
#endif

// 符号表偏移值
#ifndef N_SYMOFF
#define N_SYMOFF(x) (N_DRELOFF(x) + (x).a_drsize)
#endif

// 字符串信息偏移值
#ifndef N_STROFF
#define N_STROFF(x) (N_SYMOFF(x) + (x).a_syms)
#endif

/* Address of text segment in memory after it is loaded.  */
// 代码段加载到内存中后的地址
#ifndef N_TXTADDR
#define N_TXTADDR(x) 0
#endif

/* Address of data segment in memory after it is loaded.
   Note that it is up to you to define SEGMENT_SIZE
   on machines not listed here.  */
/**
 * 数据段加载到内存中后的地址
 * 对于本代码中未定义的机器，使用时需要根据需求定义对应的 PAGE_SIZE
*/
#if defined(vax) || defined(hp300) || defined(pyr)
#define SEGMENT_SIZE PAGE_SIZE
#endif
#ifdef	hp300
#define	PAGE_SIZE	4096
#endif
#ifdef	sony
#define	SEGMENT_SIZE	0x2000
#endif	/* Sony.  */
#ifdef is68k
#define SEGMENT_SIZE 0x20000
#endif
#if defined(m68k) && defined(PORTAR)
#define PAGE_SIZE 0x400
#define SEGMENT_SIZE PAGE_SIZE
#endif

#define PAGE_SIZE 4096
#define SEGMENT_SIZE 1024

// 以段为界的大小
#define _N_SEGMENT_ROUND(x) (((x) + SEGMENT_SIZE - 1) & ~(SEGMENT_SIZE - 1))

// 代码段尾地址
#define _N_TXTENDADDR(x) (N_TXTADDR(x)+(x).a_text)

// 数据开始地址
#ifndef N_DATADDR
#define N_DATADDR(x) \
    (N_MAGIC(x)==OMAGIC? (_N_TXTENDADDR(x)) \
     : (_N_SEGMENT_ROUND (_N_TXTENDADDR(x))))
#endif

/* Address of bss segment in memory after it is loaded.  */
// bss 段加载到内存以后的地址
#ifndef N_BSSADDR
#define N_BSSADDR(x) (N_DATADDR(x) + (x).a_data)
#endif
#ifndef N_NLIST_DECLARED
/**
 * 符号表数组中表项数据结构
*/
struct nlist {
  union {
    char *n_name; // 内存中字符串的指针
    struct nlist *n_next;
    long n_strx; // 含有本符号的名称在字符串表中的字节偏移值，当程序使用 nlist() 函数访问一个符号表时，该字段被替换为 n_name 字段
  } n_un;
  unsigned char n_type; // 用于链接程序确定如何更新符号的值；使用屏蔽可以将该字段分割成三个子字段，对于 N_EXT 类型位置位，链接程序将其看作是外部的符号，并且允许其他二进制文件对其进行的引用
  char n_other; // 按照 n_type 确定的段，提供有关符号重定位操作的符号独立位信息
  short n_desc; // 保留给调式程序处理，链接程序不对其进行处理
  unsigned long n_value; // 含有符号的值，对于代码、数据或 BSS 符号，这是一个地址，对于其他符号则是，值可以是任意的 
};
#endif

// 定义 nlist 中 n_type 字段中各种类型常量符号
#ifndef N_UNDF
#define N_UNDF 0 // 未定义的符号，链接程序必须在其他二进制文件定义一个同名外部符号，以确定该符号的绝对数据值
#endif
#ifndef N_ABS
#define N_ABS 2 // 绝对符号，链接程序不会更新
#endif
#ifndef N_TEXT
#define N_TEXT 4 // 代码符号，符号值为代码地址，链接程序在合并二进制目标文件时更新该值
#endif
#ifndef N_DATA
#define N_DATA 6 // 数据符号，与 N_TEXT 一致，但却只是用于数据地址；对应代码和数据的值不是文件的偏移值而是地址
#endif
#ifndef N_BSS
#define N_BSS 8 // BSS 符号，与代码和数据符号类似，但在二进制目标文件中没有对应的偏移
#endif
#ifndef N_COMM
#define N_COMM 18
#endif
#ifndef N_FN
#define N_FN 15  // 文件名符号，在合并二进制文件时，链接程序会将该符号插入在二进制文件中的符号之前，符号名称就是给予链接程序的文件名，其值是二进制文件中第一个代码段地址
#endif

#ifndef N_EXT
#define N_EXT 1
#endif
#ifndef N_TYPE
#define N_TYPE 036
#endif
#ifndef N_STAB
#define N_STAB 0340 // 屏蔽码用于选择符号调试程序感兴趣的位，其值在 stab() 函数中进行说明
#endif

/* The following type indicates the definition of a symbol as being
   an indirect reference to another symbol.  The other symbol
   appears as an undefined reference, immediately following this symbol.

   Indirection is asymmetrical.  The other symbol's value will be used
   to satisfy requests for the indirect symbol, but not vice versa.
   If the other symbol does not have a definition, libraries will
   be searched to find a definition.  */
/**
 * 下面类型指出了符号的定义作为对另一符号的间接引用；
 * 紧接该符号的其他符号呈现为未定义的引用，间接性是不对称的
*/
#define N_INDR 0xa

/* The following symbols refer to set elements.
   All the N_SET[ATDB] symbols with the same name form one set.
   Space is allocated for the set in the text section, and each set
   element's value is stored into one word of the space.
   The first word of the space is the length of the set (number of elements).

   The address of the set is made into an N_SETV symbol
   whose name is the same as the name of the set.
   This symbol acts like a N_DATA global symbol
   in that it can satisfy undefined external references.  */

/* These appear as input to LD, in a .o file.  */
#define	N_SETA	0x14		/* Absolute set element symbol */ // 绝对集合元素符号
#define	N_SETT	0x16		/* Text set element symbol */ // 代码集合元素符号
#define	N_SETD	0x18		/* Data set element symbol */ // 数据集合元素符号
#define	N_SETB	0x1A		/* Bss set element symbol */ // Bss 集合元素符号

/* This is output from LD.  */
#define N_SETV	0x1C		/* Pointer to set vector in data area.  */

#ifndef N_RELOCATION_INFO_DECLARED

/* This structure describes a single relocation to be performed.
   The text-relocation section of the file is a vector of these structures,
   all of which apply to the text section.
   Likewise, the data-relocation section applies to the data section.  */

/**
 * 重定位信息的数据结构
*/
struct relocation_info
{
  /*需要链接程序处理（编辑）的指针的字节偏移值，代码段偏移值是从代码段开始处计算的，数据段偏移值则是从数据段开始处计算的，链接程序会根据已存储的地址对其进行处理*/
  int r_address; // 
  /* 含有符号表中一个符号结构的序号值（不是字节偏移值），链接程序在计算出符号的绝对地址之后，就将该地址加到正在进行重定位的指针上 */
  unsigned int r_symbolnum:24;
  /* 设置了该位时，系统就认为正在更新一个指针，该指针使用 pc 相关的寻址方式，属于机器码指令部分，当运行程序使用这个被重定位的指针时，该指针的地址被隐式加到该指针上*/
  unsigned int r_pcrel:1; // 非0表明该偏移与pc相关，因此在其本身的地址空间以及符号或指定的节改变时，需要被重定位
  /**
   * 该字段含有指针长度的 2 的次方值，
   * 0 - 1 字节长
   * 1 - 2 字节长
   * 2 - 4 字节长
   * */
  unsigned int r_length:2;
   /**
    * 1 => 置位时，表示重定位需要一个外部引用，此时程序必须使用一个符号地址来更新相应指针；
    * 0 => 重定位是局部的，链接程序更新指针以反映各个段加载地址的相应变化，而不是反应一个符号值的变化（此时 r_symbolnum 为 N_TEXT、N_DATA、N_ASS 或 N_BSS）；
   */
  unsigned int r_extern:1; 
   /**
    * 最后四位
    * r_baserel:1 => 置位时，r_symbolnum 字段指定的符号将被重定位全局符号偏移表中的一个偏移值（运行时，全局表该偏移处被设置为符号的地址）
    * r_jmptable:1 => 置位时，r_symbolnum 字段指定的符号将被重定位为过程链接表中的一个偏移值
    * r_relative:1 => 置位说明此重定位与代码映像文件在运行时被加载的地址有关，这类重定向仅在共享目标文件中出现
    * r_copy:1 => 置位后该重定位记录指定了一个符号，该符号的内通将被复制到 r_address 所指定的地方，该复制操作是通过共享目标模块中的一个合适的数据项中的运行时刻链接程序完成的
   */
  unsigned int r_pad:4;
};
#endif /* no N_RELOCATION_INFO_DECLARED.  */


#endif /* __A_OUT_GNU_H__ */
