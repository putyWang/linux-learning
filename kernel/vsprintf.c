/*
 *  linux/kernel/vsprintf.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* vsprintf.c -- Lars Wirzenius & Linus Torvalds. */
/*
 * Wirzenius wrote this portably, Torvalds fucked it up :-)
 */

#include <stdarg.h>
#include <string.h>

/* we use this so that we can do without the ctype library */
#define is_digit(c)	((c) >= '0' && (c) <= '9') // 判断字符是否为数字字符

/**
 * 将字符数字串转换为整数
 * @param *s 需要转换的字符串指针
 * @return 转换出的整数
*/
static int skip_atoi(const char **s)
{
	int i=0;

	while (is_digit(**s))
		i = i*10 + *((*s)++) - '0';
	return i;
}

// 定义转换类型的各种符号常数
#define ZEROPAD	1		// 填充 0
#define SIGN	2		// 无符号/符号长整数
#define PLUS	4		// 显示加
#define SPACE	8		// 如是加，则置空格
#define LEFT	16		// 左调整
#define SPECIAL	32		// 0x
#define SMALL	64		// 使用小写字符

/**
 * 除操作
 * @param n 被除数（商保存在 n 中）
 * @param base 除数
 * @return 余数
*/
#define do_div(n,base) ({ \
int __res; \
__asm__("divl %4":"=a" (n),"=d" (__res):"0" (n),"1" (0),"r" (base)); \
__res; })

/**
 * 将整数转化为指定进制的字符串
 * @param str 转换的字符串指针
 * @param num 整数
 * @param base 进制
 * @param size 字符串长度
 * @param precision 数字长度（精度）
 * @param type 类型选项（上面常数类型）
 * @return 
*/
static char * number(char * str, int num, int base, int size, int precision
	,int type)
{
	char c,sign,tmp[36];
	const char *digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int i;
	// 小写字母，定义为小写字母制
	if (type&SMALL) digits="0123456789abcdefghijklmnopqrstuvwxyz";
	// 指出要左调整（靠左边界），则屏蔽填零标志
	if (type&LEFT) type &= ~ZEROPAD;
	// 本程序只处理基数在 2-32 之间的数
	if (base<2 || base>36)
		return 0;
	// 类型指出要填零，则置字符变量 c='0'，否则 c 等于空格字符
	c = (type & ZEROPAD) ? '0' : ' ' ;
	// 类型指出是带符号数并且数值 num 小于 0，则置符号变量 sign=负号，并使 num 取绝对值
	if (type&SIGN && num<0) {
		sign='-';
		num = -num;
	// 是否显示 + 号
	} else
		sign=(type&PLUS) ? '+' : ((type&SPACE) ? ' ' : 0);
	// 带符号，则宽度值减一
	if (sign) size--;
	// 特殊类型转换，则对于十六进制宽度再减少 2 位（用于 0x），对于八进制宽度将 1 位（用于八进制转换结果前放一个零）
	if (type&SPECIAL)
		if (base==16) size -= 2;
		else if (base==8) size--;
	// 如果 num 为 0，则临时字符串='0'；否则根据指定基数将数值 num 转换成字符形式
	i=0;
	if (num==0)
		tmp[i++]='0';
	else while (num!=0)
		tmp[i++]=digits[do_div(num,base)];
	// 若数值字符个数大于精度值，则将精度值扩展为数字个数值，宽度值减去用于存放数字字符的个数
	if (i>precision) precision=i;
	size -= precision;
	// 若类型中没有填零和左调整标志，则在 str 中首先填放剩余宽度值指出的空格数，若需带符号位，则存入符号
	if (!(type&(ZEROPAD+LEFT)))
		while(size-->0)
			*str++ = ' ';
	if (sign)
		*str++ = sign;
	// 类型指出特殊转换，则对于八进制转换结果头一位放置一个 ‘0’，面对与十六进制则存放‘0x’
	if (type&SPECIAL)
		if (base==8)
			*str++ = '0';
		else if (base==16) {
			*str++ = '0';
			*str++ = digits[33];
		}
	// 没有左调整标志，则在剩余宽度中存放 c 字符（'0' 或者空格）
	if (!(type&LEFT))
		while(size-->0)
			*str++ = c;
	// 使用 '0' 将字符串填充到指定精度
	while(i<precision--)
		*str++ = '0';
	// 将转数值换好的数字字符填入 str 中，共 i 个
	while(i-->0)
		*str++ = tmp[i];
	// 若宽度值仍大于零，则表示类型标志中有左靠齐标志标志，则在剩余宽度中放入空格
	while(size-->0)
		*str++ = ' ';
	return str;
}

/**
 * 送格式化输出到字符串中
 * @param buf 输出字符串缓冲区
 * @param fmt 格式字符串
 * @param args 个数变化的值
 * @return 
*/
int vsprintf(char *buf, const char *fmt, va_list args)
{
	int len;
	int i;
	char * str;
	char *s;
	int *ip;

	int flags;		/* flags to number() */

	int field_width;	/* width of output field */
	int precision;		/* min. # of digits for integers; max
				   number of chars for from string */
	int qualifier;		/* 'h', 'l', or 'L' for integer fields */

	for (str=buf ; *fmt ; ++fmt) {
		// % 号直接跳过
		if (*fmt != '%') {
			*str++ = *fmt;
			continue;
		}
			
		/* process flags */
		flags = 0;
		repeat:
			++fmt;
			// 根据特定字符设置特定标志字段
			switch (*fmt) {
				case '-': flags |= LEFT; goto repeat;
				case '+': flags |= PLUS; goto repeat;
				case ' ': flags |= SPACE; goto repeat;
				case '#': flags |= SPECIAL; goto repeat;
				case '0': flags |= ZEROPAD; goto repeat;
				}
		
		// 取参数字段宽度域值放入 field_width 变量中
		field_width = -1;
		// 宽度域中是数值则直接取其为宽度值
		if (is_digit(*fmt))
			field_width = skip_atoi(&fmt);
		// 宽度域是字符 '*'，表示下一参数指定块度，调用 va_arg 取宽度值
		else if (*fmt == '*') {
			/* it's the next argument */
			field_width = va_arg(args, int);
			// 若此时宽度值 <0,则该负数表示其带有标志域'-'标志（左靠齐），因此需在标志变量中添入该标志，并将字段宽度值取为其绝对值
			if (field_width < 0) {
				field_width = -field_width;
				flags |= LEFT;
			}
		}

		// 取格式转换串精度域，并放入 precision 变量中
		precision = -1;
		// 开始标志为'.'，处理过程与上面宽度域类似
		if (*fmt == '.') {
			++fmt;	
			if (is_digit(*fmt))
				precision = skip_atoi(&fmt);
			// 精度域中是字符'*'，表示下一个参数指定精度
			else if (*fmt == '*') {
				/* it's the next argument */
				precision = va_arg(args, int); // 获取参数的精度值
			}
			// 此时宽度值小于 0，字段精度值取为 0
			if (precision < 0)
				precision = 0;
		}

		// 分析长度修饰符，并将其存入 qualifer 变量
		qualifier = -1;
		if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
			qualifier = *fmt;
			++fmt;
		}

		// 分析转换指示符
		switch (*fmt) {
		// 指示符为 'c'，则表示对应参数是字符
		case 'c':
			// 如果此时标志域表明不是左对齐，则该字段前面放入宽度域值 -1 个空格字符，然后再放入参数字符
			if (!(flags & LEFT))
				while (--field_width > 0)
					*str++ = ' ';
			*str++ = (unsigned char) va_arg(args, int);
			// 宽度域还大于 0，则表示为左对齐，则在参数字符后面添加宽度域 - 1 个空格字符
			while (--field_width > 0)
				*str++ = ' ';
			break;
		// 转换指示符是's'，表示对应参数为字符串
		case 's':
			// 取字符串的长度，若超过了精度域值，则扩展精度域 = 字符串长度
			s = va_arg(args, char *);
			len = strlen(s);
			if (precision < 0)
				precision = len;
			else if (len > precision)
				len = precision;
			// 标志域表明不是左靠齐，则该字段前放入（宽度值 - 字符串长度）个空格字符，然后放入参数字符串
			if (!(flags & LEFT))
				while (len < field_width--)
					*str++ = ' ';
			for (i = 0; i < len; ++i)
				*str++ = *s++;
			// 宽度域还大于 0，表示左靠齐，则在参数字符串后面添加（宽度值-字符串长度）个空格字符
			while (len < field_width--)
				*str++ = ' ';
			break;
		// 格式转换符为 'o'，表示需将对应的参数转换成八进制数的字符串，调用 number() 函数处理
		case 'o':
			str = number(str, va_arg(args, unsigned long), 8,
				field_width, precision, flags);
			break;
		// 格式转换符为 'p'，表示对应参数是一个指针
		case 'p':
			// 该参数没有设置宽度域，默认宽度域设置为8，并且需要添 0
			if (field_width == -1) {
				field_width = 8;
				flags |= ZEROPAD;
			}
			// 调用 number 函数进行处理 
			str = number(str,
				(unsigned long) va_arg(args, void *), 16,
				field_width, precision, flags);
			break;
		// 格式转换指示是 'x' 或 'X'，表明对应参数需打印成十六进制数输出，'x'表示用小写字符表示
		case 'x':
			flags |= SMALL;
		case 'X':
			str = number(str, va_arg(args, unsigned long), 16,
				field_width, precision, flags);
			break;
		// 格式转换指示是 'd' 、'i' 或 'u',表明对应参数为整数
		// 'd' 和 'i' 代表符号整数，'u'代表无符号整数
		case 'd':
		case 'i':
			flags |= SIGN;
		case 'u':
			str = number(str, va_arg(args, unsigned long), 10,
				field_width, precision, flags);
			break;
		// 格式转换指示是 'n'，表示要把到目前为止转换输出的字符数保存到对应参数执政指定的位置中
		case 'n':
			ip = va_arg(args, int *); // 获取该参数指针，
			*ip = (str - buf); // 将已经转换好的字符数存入该指针所指的位置
			break;

		default:
			// 格式转换指示不是 '%'，则表示格式字符串有错，直接将一个 '%' 写入到输出串中
			if (*fmt != '%')
				*str++ = '%';
			// 格式转换符的位置处还有字符，则也直接将该字符写入输出串中，并返回到 107 行继续处理格式字符串，否则表示已经处理到格式字符串的结尾处，退出循环
			if (*fmt)
				*str++ = *fmt;
			else
				--fmt;
			break;
		}
	}
	*str = '\0'; // 最后在转换好的字符串结尾处添上 null
	return str-buf;
}
