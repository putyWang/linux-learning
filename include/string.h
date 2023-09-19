#ifndef _STRING_H_
#define _STRING_H_

#ifndef NULL
#define NULL ((void *) 0)
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

extern char * strerror(int errno);

/*
 * This string-include defines all string functions as inline
 * functions. Use gcc. It also assumes ds=es=data space, this should be
 * normal. Most of the string-functions are rather heavily hand-optimized,
 * see especially strtok,strstr,str[c]spn. They should work, but are not
 * very easy to understand. Everything is done entirely within the register
 * set, making the functions fast and clean. String instructions have been
 * used through-out, making for "slightly" unclear code :-)
 *
 *		(C) 1991 Linus Torvalds
 */
 
 /**
  * 将源字符串复制到另一字符串，直到遇到 NULL
  * @param dest 目标字符串
  * @param src 源字符串
 */
extern inline char * strcpy(char * dest,const char *src)
{
__asm__("cld\n" // 清方向位
	"1:\tlodsb\n\t" // 复制 ds[esi] 处一字节 -> al 并更新 esi
	"stosb\n\t"  // 将 al 中国呢字节存储至 es[edi] 所指向内存
	"testb %%al,%%al\n\t" // 为空结束，不为空继续
	"jne 1b"
	::"S" (src),"D" (dest):"si","di","ax");
return dest;
}

/**
 * 将源字符串中指定数目的字符复制到另一字符串中（中间遇到 NULL 也会停止）
 * @param dest 目标字符串
 * @param src 源字符串
 * @param count 复制字符数
*/
extern inline char * strncpy(char * dest,const char *src,int count)
{
__asm__("cld\n"
	"1:\tdecl %2\n\t"
	"js 2f\n\t"
	"lodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n\t"
	"rep\n\t"
	"stosb\n"
	"2:"
	::"S" (src),"D" (dest),"c" (count):"si","di","ax","cx");
return dest;
}

/**
  * 将源字符串添加到目标字符串尾部
  * @param dest 目标字符串
  * @param src 源字符串
 */
extern inline char * strcat(char * dest,const char * src)
{
__asm__("cld\n\t"
	"repne\n\t" // 比较 al 与 es[edi] 字节，并更新 edi ++， 直到找到目的串中是 0 的字节，此时 edi 已指向后一字节
	"scasb\n\t"
	"decl %1\n" // 让 es[edi] 指向 0 值字节
	"1:\tlodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b"
	::"S" (src),"D" (dest),"a" (0),"c" (0xffffffff):"si","di","ax","cx");
return dest;
}

/**
 * 将源字符串中指定数量字符添加到目标字符串尾部（中间遇到 NULL 也会停止）
 * @param dest 目标字符串
 * @param src 源字符串
 * @param count 复制字符数
*/
extern inline char * strncat(char * dest,const char * src,int count)
{
__asm__("cld\n\t"
	"repne\n\t"
	"scasb\n\t"
	"decl %1\n\t"
	"movl %4,%3\n"
	"1:\tdecl %3\n\t"
	"js 2f\n\t"
	"lodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n"
	"2:\txorl %2,%2\n\t"
	"stosb"
	::"S" (src),"D" (dest),"a" (0),"c" (0xffffffff),"g" (count)
	:"si","di","ax","cx");
return dest;
}

/**
 * 对比两个字符串
 * @param cs 对比字符串 1
 * @param ct 对比字符串 2
 * @return 串1 > 串2：1，串1 = 串2：0，串1 < 串2：-1，
*/
extern inline int strcmp(const char * cs,const char * ct)
{
register int __res __asm__("ax");
__asm__("cld\n"
	"1:\tlodsb\n\t" // 取字符串2 一个字节 ds:[esi] => al,并且 esi ++ 
	"scasb\n\t"  // 比较 al 与 es[edi] 字节，并更新 edi ++
	"jne 2f\n\t" // 不等向前跳转至 2
	"testb %%al,%%al\n\t"
	"jne 1b\n\t" // al 不等于 0 继续对比下一字符
	"xorl %%eax,%%eax\n\t" // 相等，将 ax 设置为 0，结束
	"jmp 3f\n"
	"2:\tmovl $1,%%eax\n\t" // 将 ax 设置为 1
	"jl 3f\n\t" // 前面对比中 1 > 2 直接返回
	"negl %%eax\n" // 否则将 ax 中的值置 -1
	"3:"
	:"=a" (__res):"D" (cs),"S" (ct):"si","di");
return __res;
}

/**
 * 对比两个字符串头前指定个数字符串
 * @param cs 对比字符串 1
 * @param ct 对比字符串 2
 * @param count 数量
 * @return 串1 > 串2：1，串1 = 串2：0，串1 < 串2：-1，
*/
extern inline int strncmp(const char * cs,const char * ct,int count)
{
register int __res __asm__("ax");
__asm__("cld\n"
	"1:\tdecl %3\n\t" // count -- 
	"js 2f\n\t" // count < 0 时，向前跳转至标号 2
	"lodsb\n\t"
	"scasb\n\t"
	"jne 3f\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n"
	"2:\txorl %%eax,%%eax\n\t"
	"jmp 4f\n"
	"3:\tmovl $1,%%eax\n\t"
	"jl 4f\n\t"
	"negl %%eax\n"
	"4:"
	:"=a" (__res):"D" (cs),"S" (ct),"c" (count):"si","di","cx");
return __res;
}

/**
 * 查询目标字符串中第一个匹配的字符
 * @param s 目标字符串
 * @param c 需要的字符
 * @return  匹配到字符的指针，
*/
extern inline char * strchr(const char * s,char c)
{
register char * __res __asm__("ax");
__asm__("cld\n\t"
	"movb %%al,%%ah\n" // 将预比较的字符移到 ah
	"1:\tlodsb\n\t"
	"cmpb %%ah,%%al\n\t" // 对比当前字符与ah
	"je 2f\n\t" // 相等向前跳转至 2
	"testb %%al,%%al\n\t"
	"jne 1b\n\t" // 目标字符串未到末尾，重复上述操作
	"movl $1,%1\n" // 未找到时 esi 返回 0
	"2:\tmovl %1,%0\n\t" // 将指向匹配字符后一个字节处的指针值放入 eax
	"decl %0" // 将指针后退一位调整为指向匹配的字符
	:"=a" (__res):"S" (s),"0" (c):"si");
return __res;
}
/**
 * 查询目标字符串中指定字符最后一次出现的地方
 * @param s 目标字符串
 * @param c 需要的字符
 * @return  匹配到字符的指针
*/
extern inline char * strrchr(const char * s,char c)
{
register char * __res __asm__("dx");
__asm__("cld\n\t"
	"movb %%al,%%ah\n"
	"1:\tlodsb\n\t"
	"cmpb %%ah,%%al\n\t"
	"jne 2f\n\t"
	"movl %%esi,%0\n\t"
	"decl %0\n"
	"2:\ttestb %%al,%%al\n\t"
	"jne 1b"
	:"=d" (__res):"0" (0),"S" (s),"a" (c):"ax","si");
return __res;
}

/**
 * 在字符串 1 中查找第一个包含字符串 2 中所有字符的字符串序列
 * @param cs 字符串 1
 * @param ct 字符串 2
 * @return 匹配的字符串序列
*/
extern inline int strspn(const char * cs, const char * ct)
{
register char * __res __asm__("si");
__asm__("cld\n\t"
	"movl %4,%%edi\n\t" 
	"repne\n\t"
	"scasb\n\t"
	"notl %%ecx\n\t" // 如果不相等就继续比较（ecx 逐步递减）
	"decl %%ecx\n\t" // ecx 每位取反
	"movl %%ecx,%%edx\n" // ecx --，得2得长度值
	"1:\tlodsb\n\t" // 将串 2 的长度值暂存在 edx 中
	"testb %%al,%%al\n\t" // 该字符是否为串 1 的结尾
	"je 2f\n\t" // 如果是，则直接跳过
	"movl %4,%%edi\n\t" // 将 ct 存储到 edi 之中
	"movl %%edx,%%ecx\n\t" // 将串 2 的长度值放入 cx 中
	"repne\n\t" // 比较 al 与串 2 中字符 es:[edi], 并且 edi ++
	"scasb\n\t" // 不相等继续比较
	"je 1b\n" // 相等向后跳转至 1 处
	"2:\tdecl %0" // esi --，指向最后一个包含在 串2 中的字符
	:"=S" (__res):"a" (0),"c" (0xffffffff),"0" (cs),"g" (ct)
	:"ax","cx","dx","di");
return __res-cs; // 福慧字符序列的长度值
}

/**
 * 在字符串 1 中查找一个不包含字符串 2 的任意字符的首个序列
 * @param cs 字符串 1
 * @param ct 字符串 2
 * @return 匹配的字符串序列
*/
extern inline int strcspn(const char * cs, const char * ct)
{
register char * __res __asm__("si");
__asm__("cld\n\t"
	"movl %4,%%edi\n\t"
	"repne\n\t"
	"scasb\n\t"
	"notl %%ecx\n\t"
	"decl %%ecx\n\t"
	"movl %%ecx,%%edx\n"
	"1:\tlodsb\n\t"
	"testb %%al,%%al\n\t"
	"je 2f\n\t"
	"movl %4,%%edi\n\t"
	"movl %%edx,%%ecx\n\t"
	"repne\n\t"
	"scasb\n\t"
	"jne 1b\n"
	"2:\tdecl %0"
	:"=S" (__res):"a" (0),"c" (0xffffffff),"0" (cs),"g" (ct)
	:"ax","cx","dx","di");
return __res-cs;
}

/**
 * 在字符串 1 中查找首个包含在字符串 2 的任何字符
 * @param cs 字符串 1
 * @param ct 字符串 2
 * @return 匹配的字符串序列
*/
extern inline char * strpbrk(const char * cs,const char * ct)
{
register char * __res __asm__("si");
__asm__("cld\n\t"
	"movl %4,%%edi\n\t"
	"repne\n\t"
	"scasb\n\t"
	"notl %%ecx\n\t"
	"decl %%ecx\n\t"
	"movl %%ecx,%%edx\n"
	"1:\tlodsb\n\t"
	"testb %%al,%%al\n\t"
	"je 2f\n\t"
	"movl %4,%%edi\n\t"
	"movl %%edx,%%ecx\n\t"
	"repne\n\t"
	"scasb\n\t"
	"jne 1b\n\t"
	"decl %0\n\t"
	"jmp 3f\n"
	"2:\txorl %0,%0\n"
	"3:"
	:"=S" (__res):"a" (0),"c" (0xffffffff),"0" (cs),"g" (ct)
	:"ax","cx","dx","di");
return __res;
}

/**
 * 在字符串 1 中查找第一个与字符串 2 匹配的字符串序列
 * @param cs 字符串 1
 * @param ct 字符串 2
 * @return 匹配的字符串序列
*/
extern inline char * strstr(const char * cs,const char * ct)
{
register char * __res __asm__("ax");
__asm__("cld\n\t" \
	"movl %4,%%edi\n\t"
	"repne\n\t"
	"scasb\n\t"
	"notl %%ecx\n\t"
	"decl %%ecx\n\t"	/* NOTE! This also sets Z if searchstring='' */
	"movl %%ecx,%%edx\n"
	"1:\tmovl %4,%%edi\n\t"
	"movl %%esi,%%eax\n\t"
	"movl %%edx,%%ecx\n\t"
	"repe\n\t"
	"cmpsb\n\t"
	"je 2f\n\t"		/* also works for empty string, see above */
	"xchgl %%eax,%%esi\n\t"
	"incl %%esi\n\t"
	"cmpb $0,-1(%%eax)\n\t"
	"jne 1b\n\t"
	"xorl %%eax,%%eax\n\t"
	"2:"
	:"=a" (__res):"0" (0),"c" (0xffffffff),"S" (cs),"g" (ct)
	:"cx","dx","di","si");
return __res;
}

/**
 * 计算字符串长度
 * @param s 字符串
 * @return 字符串长度
*/
extern inline int strlen(const char * s)
{
register int __res __asm__("cx");
__asm__("cld\n\t"
	"repne\n\t"
	"scasb\n\t"
	"notl %0\n\t"
	"decl %0"
	:"=c" (__res):"D" (s),"a" (0),"0" (0xffffffff):"di");
return __res;
}

extern char * ___strtok; // 用于存放下面被分析字符串 1(s) 的指针

/**
 * 利用字符串 2 中的字符将字符串 1 分割成标记（tokern）序列
 * 将字符串 1 看作由 0 个或 多个单词（token）组成的序列，并由分割符字符串 2 中的一个或多个字符分开
 * @param s 字符串 1
 * @param ct 字符串 2
 * @return 匹配的字符串序列
*/
extern inline char * strtok(char * s,const char * ct)
{
register char * __res __asm__("si");
__asm__("testl %1,%1\n\t" // 测试s是否为空
	"jne 1f\n\t" // 不是说明首次调用，向前跳转到标号 1
	"testl %0,%0\n\t" // 测试 ___strtok 是否为空
	"je 8f\n\t" // 为NULL不能处理，直接向前跳转至标记 8 处结束
	"movl %0,%1\n" // 将 ebx 的指针复制到 esi 中
	"1:\txorl %0,%0\n\t" // 清空 ebx 中的指针
	"movl $-1,%%ecx\n\t" // ecx 赋值为 -1
	"xorl %%eax,%%eax\n\t" // 清空 eax
	"cld\n\t" // 清方向位
	"movl %4,%%edi\n\t" // edi 指向字符串 2
	"repne\n\t" // 比较 al 与 es[edi] 字节，并更新 edi ++， 
	"scasb\n\t" // 直到找到目的串中是 0 的字节，或者计数 ecx == 0
	"notl %%ecx\n\t" // ecx 取反
	"decl %%ecx\n\t" // ecx --，得到字符串 2 的长度值
	"je 7f\n\t"			/* empty delimeter-string */ // 字符串 2 长度为 0 时向前跳转至 7 处
	"movl %%ecx,%%edx\n" // 将字符串 2 长度存储到 edx 中
	"2:\tlodsb\n\t" // 取字符串 1 的字符 ==> al，并且 esi ++
	"testb %%al,%%al\n\t"
	"je 7f\n\t" // 字符串1 的字符为空向前跳转至 7 处
	"movl %4,%%edi\n\t" // edi 指向字符串 2
	"movl %%edx,%%ecx\n\t" // ecx 记录字符串 2 的长度
	"repne\n\t" // 将 a1 中字符与字符串 2 中的所有字符进行比较
	"scasb\n\t" // 判断该字符是否为 分割符
	"je 2b\n\t" // 是的话跳转至 2 处，继续比较字符串 1 中下一个字符
	"decl %1\n\t" // 若不是，则串1 指针指向该字符
	"cmpb $0,(%1)\n\t" // 该字符是否为 NULL
	"je 7f\n\t" // 是 NULL 说明串 1 已遍历完，向前跳转至 7 处
	"movl %1,%0\n" // 将该字符的指针存放到 ebx 之中
	"3:\tlodsb\n\t" // 取字符串 1 中 下一字符
	"testb %%al,%%al\n\t" // 是否已到字符串 1 的末尾
	"je 5f\n\t" // 到了末尾，则向前跳转至 5
	"movl %4,%%edi\n\t" // edi 指向字符串 2
	"movl %%edx,%%ecx\n\t" // ecx 记录字符串 2 的长度
	"repne\n\t" // 将 a1 中字符与字符串 2 中的所有字符进行比较
	"scasb\n\t" // 判断该字符是否为 分割符
	"jne 3b\n\t" // 是的话跳转至 3 处，继续比较字符串 1 中下一个字符
	"decl %1\n\t" // 若不是，则串1 指针指向该字符
	"cmpb $0,(%1)\n\t" // 该字符是否为 NULL
	"je 5f\n\t" // 到了末尾，则向前跳转至 5
	"movb $0,(%1)\n\t" // 不是，则使用 NULL 将该字符替换掉
	"incl %1\n\t" // 指向下一字符
	"jmp 6f\n" // 向前跳转至标号 6
	"5:\txorl %1,%1\n" // esi 清 0
	"6:\tcmpb $0,(%0)\n\t" // ebx 是否指向 NULL
	"jne 7f\n\t" // 不是的话跳转至 7
	"xorl %0,%0\n" // 清空 ebx
	"7:\ttestl %0,%0\n\t" // ebx 清空完成结束
	"jne 8f\n\t"
	"movl %0,%1\n" // 将 esi 置为 NULL
	"8:"
	:"=b" (__res),"=S" (___strtok)
	:"0" (___strtok),"1" (s),"g" (ct)
	:"ax","cx","dx","di");
return __res;
}

/**
 * 内存块复制
 * @param dest 目标地址
 * @param src 源地址
 * @param n 复制字节数
*/
extern inline void * memcpy(void * dest,const void * src, int n)
{
__asm__("cld\n\t"
	"rep\n\t"
	"movsb"
	::"c" (n),"S" (src),"D" (dest)
	:"cx","si","di");
return dest;
}

/**
 * 内存块移动（考虑方向位）
 * @param dest 目标地址
 * @param src 源地址
 * @param n 复制字节数
*/
extern inline void * memmove(void * dest,const void * src, int n)
{
if (dest<src) // dest < src 与 复制一致
__asm__("cld\n\t"
	"rep\n\t"
	"movsb"
	::"c" (n),"S" (src),"D" (dest)
	:"cx","si","di");
else // dest > src 与从末端向前复制
__asm__("std\n\t"
	"rep\n\t"
	"movsb"
	::"c" (n),"S" (src+n-1),"D" (dest+n-1)
	:"cx","si","di");
return dest;
}

/**
 * 对比两个内存块
 * @param cs 内存块 1
 * @param ct 内存块 2
 * @param count 数量
 * @return 内存块1 > 内存块2：1，内存块1 = 内存块2：0，内存块1 < 内存块2：-1，
*/
extern inline int memcmp(const void * cs,const void * ct,int count)
{
register int __res __asm__("ax");
__asm__("cld\n\t"
	"repe\n\t"
	"cmpsb\n\t"
	"je 1f\n\t"
	"movl $1,%%eax\n\t"
	"jl 1f\n\t"
	"negl %%eax\n"
	"1:"
	:"=a" (__res):"0" (0),"D" (cs),"S" (ct),"c" (count)
	:"si","di","cx");
return __res;
}

/**
 * 在n字节大小的内存块中寻找指定字符
 * @param cs 内存块开始指针
 * @param c 查询字符
 * @param count 字节数
 * @return 内存块1 > 内存块2：1，内存块1 = 内存块2：0，内存块1 < 内存块2：-1，
*/
extern inline void * memchr(const void * cs,char c,int count)
{
register void * __res __asm__("di");
if (!count)
	return NULL;
__asm__("cld\n\t"
	"repne\n\t"
	"scasb\n\t"
	"je 1f\n\t"
	"movl $1,%0\n"
	"1:\tdecl %0"
	:"=D" (__res):"a" (c),"D" (cs),"c" (count)
	:"cx");
return __res;
}

/**
 * 将字符填写指定长度的内存块
 * @param s // 内存区域
 * @param c // 填写的字符
 * @param count // 字节数
*/
extern inline void * memset(void * s,char c,int count)
{
// cld 命令清方向位
__asm__("cld\n\t" 
// 重复 stosb 操作将 al 中的字符存入 es:[edi]，并且 edi++
	"rep\n\t"
	"stosb"
	::"a" (c),"D" (s),"c" (count)
	:"cx","di");
return s;
}

#endif
