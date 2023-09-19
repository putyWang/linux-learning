#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list; // 定义 va_list 是一个字符指针类型

/* Amount of space required in an argument list for an arg of type TYPE.
   TYPE may alternatively be an expression whose type is used.  */
// 下面这句定义了取整后的 TYPE 类型的字节长度值，是 int 长度（4）的倍数
#define __va_rounded_size(TYPE)  \
  (((sizeof (TYPE) + sizeof (int) - 1) / sizeof (int)) * sizeof (int))

// va_start 函数使 AP 指向传给函数的可变参数表的第一个参数
// 在第一次调用 va_arg 或 va_end 之前，必须首先调用该函数
#ifndef __sparc__
#define va_start(AP, LASTARG) 						\
 (AP = ((char *) &(LASTARG) + __va_rounded_size (LASTARG)))
#else
#define va_start(AP, LASTARG) 						\
 (__builtin_saveregs (),						\
  AP = ((char *) &(LASTARG) + __va_rounded_size (LASTARG)))
#endif

// va_end 宏 用于被调用函数的完成一次正常返回，
// 可以修改 AP 使其在重新调用 va_start 之前不能被使用；
// va_end 必须在 va_arg 读完所有参数后再被调用
void va_end (va_list);		/* Defined in gnulib */
#define va_end(AP)

/**
 * va_arg 用于扩展表达式使其与下一个被传递参数具有相同的类型和值。
 * 对于缺省值，va_arg 可以用字符、无符号字符和浮点类型
 * 在第一次使用时，他返回表中的第一个参数，后续的每次调用都将返回表中的下一个参数
 * 这里通过先访问 AP，然后把它增加以指向下一项来实现的，va_arg 使用 TYPE 来完成访问和定位下一项，每调用一次 va_arg，他就修改 AP 以表示表中的下一参数
*/
#define va_arg(AP, TYPE)						\
 (AP += __va_rounded_size (TYPE),					\
  *((TYPE *) (AP - __va_rounded_size (TYPE))))

#endif /* _STDARG_H */
