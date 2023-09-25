/*
 *  linux/kernel/console.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	console.c
 *
 * This module implements the console io functions
 *	'void con_init(void)'
 *	'void con_write(struct tty_queue * queue)'
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 */

/*
 *  NOTE!!! We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO), but this will work even for keyboard
 * interrupts. We know interrupts aren't enabled when getting a keyboard
 * interrupt, as we use trap-gates. Hopefully all is well.
 */

/*
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 */

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/system.h>

/*
 * These are set up by the setup-routine at boot-time:
 */

#define ORIG_X				(*(unsigned char *)0x90000) 					// 光标列号
#define ORIG_Y				(*(unsigned char *)0x90001) 					// 光标行号
#define ORIG_VIDEO_PAGE		(*(unsigned short *)0x90004) 					// 显示页面
#define ORIG_VIDEO_MODE		((*(unsigned short *)0x90006) & 0xff) 			// 显示模式
#define ORIG_VIDEO_COLS 	(((*(unsigned short *)0x90006) & 0xff00) >> 8) 	//字符列数
#define ORIG_VIDEO_LINES	(25) 											// 显示行数
#define ORIG_VIDEO_EGA_AX	(*(unsigned short *)0x90008) 
#define ORIG_VIDEO_EGA_BX	(*(unsigned short *)0x9000a) 					// 显示内存大小和色彩模式
#define ORIG_VIDEO_EGA_CX	(*(unsigned short *)0x9000c) 					// 显卡特性参数

// 定义显示器单色/彩色显示模式类型符号常数
#define VIDEO_TYPE_MDA		0x10			// 单色文本
#define VIDEO_TYPE_CGA		0x11			// CGA 显示器
#define VIDEO_TYPE_EGAM		0x20			// EGA/VGA 单色
#define VIDEO_TYPE_EGAC		0x21			// EGA/VGA 彩色

#define NPAR 16								// ANSI 转义字符序列参数数组大小

extern void keyboard_interrupt(void);

static unsigned char	video_type;			// 使用的显示类型
static unsigned long	video_num_columns;	// 屏幕文本列数
static unsigned long	video_size_row;		// 每行使用的字节数
static unsigned long	video_num_lines;	// 屏幕文本行数
static unsigned char	video_page;			// 初始显示页面
static unsigned long	video_mem_start;	// 显示内存起始处指针
static unsigned long	video_mem_end;		// 显示内存尾指针
static unsigned short	video_port_reg;		// 显示控制索引寄存器端口
static unsigned short	video_port_val;		// 显示控制数据寄存器端口
static unsigned short	video_erase_char;	// 擦除字符属性与字符

static unsigned long	origin;				// 滚屏显示内存开始位置
static unsigned long	scr_end;			// 滚屏显示内存尾指针
static unsigned long	pos; 				// 当前光标位置指向的显示内存位置
static unsigned long	x,y; 				// x-当前光标所在列，y-当前光标所在行
static unsigned long	top,bottom; 		// top-当前显示最顶行号，bottom-当前显示最底行号
static unsigned long	state=0;			// ANSI 转义字符序列处理状态
static unsigned long	npar,par[NPAR];		// ANSI 转义字符序列参数个数和参数数组
static unsigned long	ques=0;
static unsigned char	attr=0x07;			// 字符属性

static void sysbeep(void);

#define RESPONSE "\033[?1;2c"				// 终端恢复 ESC-Z 或 csi0c 请求的应答，csi - 控制虚列引导码

/**
 * 跳转到指定光标位置
 * 更新当前光标位置 x，y，并修正 pos 指向光标在显示内存中对应位置
 * @param new_x 光标所在列号
 * @param new_y 光标所在行号
*/
static inline void gotoxy(unsigned int new_x,unsigned int new_y)
{
	// 更新的光标位置不能超过限制
	if (new_x > video_num_columns || new_y >= video_num_lines)
		return;
	x=new_x;
	y=new_y;
	pos=origin + y*video_size_row + (x<<1);
}

/**
 * 设置滚屏显示地址
*/
static inline void set_origin(void)
{
	cli();														// 禁止中断
	outb_p(12, video_port_reg);									// 选择显示控制数据寄存器 r12，
	outb_p(0xff&((origin-video_mem_start)>>9), video_port_val);	// 写入卷屏起始地址高字节，向右移动 9 位，表示向右移动 8 位，再除以 2（2 字节表示屏幕显示一字符），是相对于默认显示内存操作的
	outb_p(13, video_port_reg);									// 选择显示控制数据寄存器 r13，
	outb_p(0xff&((origin-video_mem_start)>>1), video_port_val);	// 写入卷屏起始地址底字节，
	sti();
}

/**
 * 向上卷动一行（屏幕窗口向下移动）
 * 将屏幕窗口向下移动一行
*/
static void scrup(void)
{	
	// 显示类型是 EGA
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		// 当移动起始行 top=0，移动最底行 bottom=video_num_lines=25 时，则表示整屏窗口向下移动
		if (!top && bottom == video_num_lines) {
			origin += video_size_row;			// 将滚屏显示内存开始位置下移一行
			pos += video_size_row;				// 光标位置指向的显示内存位置下移一行
			scr_end += video_size_row;			// 滚屏显示内存尾指针后移一行
			// 如果当前尾指针超过了实际显示内存的末端，则将屏幕内存数据移动显示内存的起始位置，并在出现的新行上填入空格字符
			if (scr_end > video_mem_end) {
				__asm__("cld\n\t"						// 清方向位
					"rep\n\t"							// 循环操作，将当前屏幕内存数据移动到显示内存起始处
					"movsl\n\t"						
					"movl _video_num_columns,%1\n\t"	// ecx = 1 行字符数
					"rep\n\t"							// 在新行上填入空格字符
					"stosw"
					::"a" (video_erase_char),
					"c" ((video_num_lines-1)*video_num_columns>>1),
					"D" (video_mem_start),
					"S" (origin)
					:"cx","di","si");
				scr_end -= origin-video_mem_start;	// 将滚屏显示内存尾指针向后移动到实际位置
				pos -= origin-video_mem_start;		// 光标位置指向的显示内存位置后移动到实际位置
				origin = video_mem_start;			// 将滚屏显示内存开始位置指向显示内存开始位置
			// 如果没有超出末端对应的指针位置，则只是将新行上填入空格字符
			} else {
				__asm__("cld\n\t"					// 清方向位
					"rep\n\t"						// 在新行上填入空格字符
					"stosw"
					::"a" (video_erase_char),
					"c" (video_num_columns),
					"D" (scr_end-video_size_row)
					:"cx","di");
			}
			set_origin();
		// 否则表示不是整屏移动，以即表示从指定行 top 开始的所有行向上移动一行（删除一行），此时直接将指定行 top 到屏幕末端所有行对应的显示内存数据向上 1 行，并在新出现的行上插入删除字符
		} else {
			__asm__("cld\n\t"						// 清方向位
				"rep\n\t"							// 循环操作，将 top+1 内存数据移动到 top 处
				"movsl\n\t"
				"movl _video_num_columns,%%ecx\n\t"	// ecx = 1 行字符数
				"rep\n\t"							// 在新行上填入空格字符
				"stosw"
				::"a" (video_erase_char),
				"c" ((bottom-top-1)*video_num_columns>>1),
				"D" (origin+video_size_row*top),
				"S" (origin+video_size_row*(top+1))
				:"cx","di","si");
		}
	}
	// 显示类型不是 EGA（是MDA）时，由于 MDA 显示控制卡会自动调整超出显示范围的情况，也即会自动翻转指针
	// 所以这里不对屏幕内容对应内存超出显示内存的情况单独处理，处理方法与 EGA 非整屏移动情况完全一样
	else		/* Not EGA/VGA */
	{
		__asm__("cld\n\t"						// 清方向位
			"rep\n\t"							// 循环操作，将 top+1 内存数据移动到 top 处
			"movsl\n\t"
			"movl _video_num_columns,%%ecx\n\t"	// ecx = 1 行字符数
			"rep\n\t"							// 在新行上填入空格字符
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*top),
			"S" (origin+video_size_row*(top+1))
			:"cx","di","si");
	}
}

/**
 * 向下移动一行（屏幕窗口向上移动）
 * 从被移动开始行的上方出现一新行
*/
static void scrdown(void)
{
	// 显示类型是 EGA
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		__asm__("std\n\t"						// 置方向位
			"rep\n\t"							// 循环操作，将 bottom 内存数据移动到 bottom-1 处
			"movsl\n\t"
			"addl $2,%%edi\n\t"					// edi 已经 -4，因为也是反向填擦除字符
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"							// 将擦除字符填入上方新行中
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*bottom-4),
			"S" (origin+video_size_row*(bottom-1)-4)
			:"ax","cx","di","si");
	}
	else		/* Not EGA/VGA */
	{
		__asm__("std\n\t"
			"rep\n\t"
			"movsl\n\t"
			"addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*bottom-4),
			"S" (origin+video_size_row*(bottom-1)-4)
			:"ax","cx","di","si");
	}
}

/**
 * 光标下移一行
*/
static void lf(void)
{
	// 移动后的光标还未到底部时，直接将光标与对应内存下移一行
	if (y+1<bottom) {
		y++;
		pos += video_size_row;
		return;
	}
	scrup(); // 已经超过底部时，整个屏幕下移一行
}

/**
 * 光标上移一行
*/
static void ri(void)
{
	// 移动前的光标还未到到顶时，直接将光标与对应内存上移一行
	if (y>top) {
		y--;
		pos -= video_size_row;
		return;
	}
	scrdown(); // 已经超过顶部时，整个屏幕上移一行
}

/**
 * 光标 x 移动到第一列
*/
static void cr(void)
{
	pos -= x<<1;
	x=0;
}

/**
 * 删除一个字符
*/
static void del(void)
{
	// 改行字符不为 0 才能删除字符
	if (x) {
		pos -= 2;
		x--;
		*(unsigned short *)pos = video_erase_char;  // 使用擦除符替换删除的字符
	}
}

/**
 * 删除屏幕上与光标位置相关的部分，以屏幕为单位
 * @param par 控制序列引导码
*/
static void csi_J(int par)
{
	long count __asm__("cx");			// 删除的字符数
	long start __asm__("di");			// 删除字符内存开始处

	// ANSI 转义符序列：'ESC[sJ]'
	switch (par) {
		case 0:							// 擦除光标到屏幕底端
			count = (scr_end-pos)>>1;
			start = pos;
			break;
		case 1:							// 删除从屏幕开始到贯标处的字符
			count = (pos-origin)>>1;
			start = origin;
			break;
		case 2: 						// 删除整个屏幕上的字符
			count = video_num_columns * video_num_lines;
			start = origin;
			break;
		default:
			return;
	}
	// 使用擦除字符填充删除的位置
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t"
		::"c" (count),
		"D" (start),"a" (video_erase_char)
		:"cx","di");
}

/**
 * 删除行内与光标位置相关的部分，以屏幕为单位
 * @param par 控制序列引导码
*/
static void csi_K(int par)
{
	long count __asm__("cx");
	long start __asm__("di");

	switch (par) {
		case 0:							// 擦除当前行光标之后的所有字符
			if (x>=video_num_columns)
				return;
			count = video_num_columns-x;
			start = pos;
			break;
		case 1:							// 删除行开始处到光标处的字符
			start = pos - (x<<1);
			count = (x<video_num_columns)?x:video_num_columns;
			break;
		case 2: 						// 删除整行字符
			start = pos - (x<<1);
			count = video_num_columns;
			break;
		default:
			return;
	}
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t"
		::"c" (count),
		"D" (start),"a" (video_erase_char)
		:"cx","di");
}

/**
 * 允许翻译（重显）
 * ANSI 转义符序列：'ESC[nm'，n=0 正常显示，1 加粗，4 加下划线， 7 反显， 27 正常显示
*/
void csi_m(void)
{
	int i;

	for (i=0;i<=npar;i++)
		switch (par[i]) {
			case 0:attr=0x07;break;
			case 1:attr=0x0f;break;
			case 4:attr=0x0f;break;
			case 7:attr=0x70;break;
			case 27:attr=0x07;break;
		}
}

/**
 * 设置光标位置
*/
static inline void set_cursor(void)
{
	cli();
	outb_p(14, video_port_reg);
	outb_p(0xff&((pos-video_mem_start)>>9), video_port_val);
	outb_p(15, video_port_reg);
	outb_p(0xff&((pos-video_mem_start)>>1), video_port_val);
	sti();
}

/**
 * 发送对终端 VT100 的响应序列，将响应序列放入读缓冲队列
 * @param tty tty 响应队列
*/
static void respond(struct tty_struct * tty)
{
	char * p = RESPONSE;

	cli();						// 关中断
	while (*p) {				
		PUTCH(*p,tty->read_q);	// 循环将字符队列放入写队列
		p++;
	}
	sti();						// 开中断
	copy_to_cooked(tty);		// 转换成规范模式（放入辅助队列）
}

/**
 * 在光标处插入一个空字符
*/
static void insert_char(void)
{
	int i=x;
	unsigned short tmp, old = video_erase_char;
	unsigned short * p = (unsigned short *) pos;

	// 从光标开始的所有字符右移一格，并将擦除字符插入到光标所在处
	while (i++<video_num_columns) {
		tmp=*p;
		*p=old;
		old=tmp;
		p++;
	}
}

/**
 * 在光标处插入一行，将屏幕从光标所在行到屏幕底向下卷动一行
*/
static void insert_line(void)
{
	int oldtop,oldbottom;

	oldtop=top;					// 保存旧顶部位置
	oldbottom=bottom;			// 保存旧底部位置
	top=y;						// 设置卷动开始行
	bottom = video_num_lines;	// 设置卷动结束行
	scrdown();					// 从光标开始处，屏幕内容向上滚动一行
	top=oldtop;					// 恢复原 top，bottom 值
	bottom=oldbottom;
}

/**
 * 删除光标处的一个字符
*/
static void delete_char(void)
{
	int i;
	unsigned short * p = (unsigned short *) pos;

	if (x>=video_num_columns)
		return;
	i = x;
	// 循环将光标后的所有字符全部向前移动一部分
	while (++i < video_num_columns) {
		*p = *(p+1);
		p++;
	}
	*p = video_erase_char; // 将尾指针处设置为擦除符
}

/**
 * 删除光标所在行
*/
static void delete_line(void)
{
	int oldtop,oldbottom;

	oldtop=top;
	oldbottom=bottom;
	top=y;
	bottom = video_num_lines;
	scrup();					// 向上卷动一行，覆盖光标上一行
	top=oldtop;
	bottom=oldbottom;
}

/**
 * 在光标处插入指定个数空格
 * @param nr 插入字符数
*/
static void csi_at(unsigned int nr)
{
	// 插入字符数只能为 1 到 行中字符数
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_char();
}

/**
 * 在光标处插入指定个数空行
 * @param nr 插入行数
*/
static void csi_L(unsigned int nr)
{
	// 插入行数只能为 1 到 屏幕最大行
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_line();
}

/**
 * 在光标处删除指定个数字符
 * @param nr 删除字符数
*/
static void csi_P(unsigned int nr)
{
	// 删除字符数只能为 1 到 行中字符数
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		delete_char();
}

/**
 * 在光标处删除指定个数行
 * @param nr 删除行数
*/
static void csi_M(unsigned int nr)
{
	// 插入行数只能为 1 到 屏幕最大行 
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr=1;
	while (nr--)
		delete_line();
}

static int saved_x=0;	// 保存的光标行号
static int saved_y=0;	// 保存的光标列号

/**
 * 保存当前光标
*/
static void save_cur(void)
{
	saved_x=x;
	saved_y=y;
}

/**
 * 恢复到保存的光标位置
*/
static void restore_cur(void)
{
	gotoxy(saved_x, saved_y);
}

/**
 * 控制台写函数
 * @param tty 处理的 tty 设备结构 
*/
void con_write(struct tty_struct * tty)
{
	int nr;
	char c;

	nr = CHARS(tty->write_q); 						// 获取当前写缓冲队列中拥有的字符数 
	while (nr--) {
		GETCH(tty->write_q,c);						// 获取写缓冲队列中的一个字符，将其保存到 c 中
		switch(state) {
			case 0:									// 初始状态
				if (c>31 && c<127) {				// 字符既不是控制字符也不是扩展字符
					if (x>=video_num_columns) {		// 若当前光标处在行末端或末端以外，则将光标移到下行头列
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
					//将字符写到显示内存pos处
					__asm__("movb _attr,%%ah\n\t"
						"movw %%ax,%1\n\t"
						::"a" (c),"m" (*(short *)pos)
						:"ax");
					pos += 2; 						// 将显示位置左移两个字节
					x++;							// x 光标左移一个字符
				} else if (c==27)					// 转义字符 ESC，则转状态 state=1
					state=1;
				else if (c==10 || c==11 || c==12)	// 换行符（10）、垂直制表符 VT（11）或者换页符 FF（12），则移动光标到下一行
					lf();
				else if (c==13)						// 回车符（13），则将光标移动到头列
					cr();
				else if (c==ERASE_CHAR(tty))		// DEL（127），则将光标右边一个字符擦除（使用空格字符替代），并将光标移动到被擦除位置
					del();
				else if (c==8) {					// 是 BS（backspace， 8），则将光标左移一格，并相应调整光标对应内存位置指针 pos
					if (x) {
						x--;
						pos -= 2;
					}
				} else if (c==9) {					// 水平制表符TAB（9），则将光标移动到 8 的倍数列上，若此时光标列数超出最大列数，则将光标移动到下一行
					c=8-(x&7);
					x += c;
					pos += c<<1;
					if (x>video_num_columns) {
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
					c=9;
				} else if (c==7)					// 响铃符 BEL（7），调用蜂鸣函数，使扬声器发声
					sysbeep();
				break;
			case 1:									// 原状态为 0，且转义字符为 ESC，则跳转到状态 1 进行处理
				state=0;
				if (c=='[')							// '['，则将状态 state 置为 2
					state=2;
				else if (c=='E')					// 'E'，光标移动到下一行开始处
					gotoxy(0,y+1);
				else if (c=='M')					// 'M'，光标上移一行
					ri();
				else if (c=='D')					// 'D'，光标下移一行
					lf();
				else if (c=='Z')					// 'Z'，发送终端应答字符序列
					respond(tty);
				else if (x=='7')					// '7'，保存当前光标位置
					save_cur();
				else if (x=='8')					// '8'，恢复到之前光标位置
					restore_cur();
				break;
			case 2:									// 原状态为 1 且 转义字符为 '[' 时跳转到状态 2 进行处理
				for(npar=0;npar<NPAR;npar++)		// 将转义字符参数数组元素全置为 0
					par[npar]=0;
				npar=0;
				state=3;							// 状态 state 置为 3
				if (ques=(c=='?'))					// 字符是 '?' 时，读取下一字符进行处理
					break;
			case 3:
				if (c==';' && npar<NPAR-1) {		// ';'，且数组 par 未满，则索引值 +1
					npar++;
					break;
				} else if (c>='0' && c<='9') {		// 数字字符（'0'-'9'），将该字符转换成数值并与 npar 所索引的项组成 10 进制数
					par[npar]=10*par[npar]+c-'0';
					break;
				} else state=4;						// 不是分号与数字字符时，将状态 state 置为 4，继续处理
			case 4:
				state=0;							// 置状态 state = 0
				switch(c) {
					case 'G': case '`':				// 'G' 或 '`' 则 par[0] 中数 -1，同时跳转到当前行 par[0] 列所在位置
						if (par[0]) par[0]--;
						gotoxy(par[0],y);
						break;
					case 'A':						// 'A'，par[0] 为 0 时 +1，par[0] 代表上移行数
						if (!par[0]) par[0]++;
						gotoxy(x,y-par[0]);
						break;
					case 'B': case 'e':				// 'B' 或 'e'，par[0] 为 0 时 +1，par[0] 代表下移行数
						if (!par[0]) par[0]++;
						gotoxy(x,y+par[0]);
						break;
					case 'C': case 'a':				// 'C' 或 'a'，par[0] 为 0 时 +1，par[0] 代表右移列数
						if (!par[0]) par[0]++;
						gotoxy(x+par[0],y);
						break;
					case 'D':						// 'D'，par[0] 为 0 时 +1，par[0] 代表左移列数
						if (!par[0]) par[0]++;
						gotoxy(x-par[0],y);
						break;
					case 'E':						// 'E'，par[0] 为 0 时 +1，光标下移 par[0] 行到该行头列；
						if (!par[0]) par[0]++;
						gotoxy(0,y+par[0]);
						break;
					case 'F':						// 'F'，par[0] 为 0 时 +1，光标上移 par[0] 行到该行头列；
						if (!par[0]) par[0]++;
						gotoxy(0,y-par[0]);
						break;
					case 'd':						
						if (par[0]) par[0]--;		// 'd'，par[0] -1，光标移动到 par[0] 行本列；
						gotoxy(x,par[0]);
						break;
					case 'H': case 'f':				// 'H' 或 'f' 则 par[0] 与 par[1] 中数 -1，同时跳转到 par[0] 行、par[1] 列所在位置
						if (par[0]) par[0]--;
						if (par[1]) par[1]--;
						gotoxy(par[1],par[0]);
						break;
					case 'J':						// 'J'，par[0] 指定行相关删除方式；
						csi_J(par[0]);
						break;
					case 'K':						// 'K'， par[0] 指定行内相关删除方式；
						csi_K(par[0]);
						break;
					case 'L':						// 'L'， par[0] 指定插入空行数；
						csi_L(par[0]);
						break;
					case 'M':						// 'M'， par[0] 指定删除行数；
						csi_M(par[0]);
						break;
					case 'P':						// 'P'， par[0] 指定删除字符数；
						csi_P(par[0]);
						break;
					case '@':						// '@'， par[0] 指定插入空格字符数；
						csi_at(par[0]);
						break;
					case 'm':						// 'm'， 改变光标处字符的显示属性
						csi_m();
						break;
					case 'r':						// 'r'， 表示用 par[0] 与 par[1] 设置起始行号与终止行号
						if (par[0]) par[0]--;
						if (!par[1]) par[1] = video_num_lines;
						if (par[0] < par[1] &&
						    par[1] <= video_num_lines) {
							top=par[0];
							bottom=par[1];
						}
						break;
					case 's':						// 's'， 保存当前光标位置
						save_cur();
						break;
					case 'u':						// 'u'，恢复到之前光标位置
						restore_cur();
						break;
				}
		}
	}
	set_cursor();									// 根据上面设置的光标位置，通知显示器控制器改变后的光标位置
}

/*
 *  void con_init(void);
 *
 * This routine initalizes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequece.
 *
 * Reads the information preserved by setup.s to determine the current display
 * type and sets everything accordingly.
 */
/**
 * 该子程序只是初始化控制台中断控制
 * 读取 setup.s 中保存的信息，已确定当前显示器类型，并设置所有相关参数
*/
void con_init(void)
{
	register unsigned char a;
	char *display_desc = "????";
	char *display_ptr;

	video_num_columns = ORIG_VIDEO_COLS; 			// 显示字符列数
	video_size_row = video_num_columns * 2; 		// 每行需显示的字节数
	video_num_lines = ORIG_VIDEO_LINES; 			// 显示字符行数
	video_page = ORIG_VIDEO_PAGE; 					// 显示字符页数
	video_erase_char = 0x0720; 						// 擦除字符 （0x20 显示字符，0x07 是属性）
	
	if (ORIG_VIDEO_MODE == 7)						// 显示模式 7 表示单色显示器
	{
		video_mem_start = 0xb0000; 					//设置单显映像内存起始地址
		video_port_reg = 0x3b4; 					// 设置单显索引寄存器接口
		video_port_val = 0x3b5; 					// 设置单显数据寄存器接口
		// 通过 setup.s 中 int 0x10 功能 0x12 获取的显示模式信息，判断是单色显卡还是彩色显卡
		// 值不为 0x10 时是 单色 EGA 卡
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAM;			// EGA 单色卡
			video_mem_end = 0xb8000;				// 使用内存镜像尾地址为 0xb8000
			display_desc = "EGAm";					// 显示器描述字符串尾 EGAm
		}
		// 等于 0x10 时为单色 MDA 显卡
		else
		{
			video_type = VIDEO_TYPE_MDA;			// MDA 单色卡
			video_mem_end	= 0xb2000;				// 使用内存镜像尾地址为 0xb2000
			display_desc = "*MDA";					// 显示器描述字符串尾 *MDA
		}
	}
	// 彩色模式
	else								/* If not, it is color. */
	{
		video_mem_start = 0xb8000;					// 彩色模式内存开始地址为 0xb8000
		video_port_reg	= 0x3d4;					// 设置彩色显示索引寄存器接口
		video_port_val	= 0x3d5;					// 设置彩色显示数据寄存器接口
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)		// 值不为 0x10 时是 EGA 彩色卡
		{
			video_type = VIDEO_TYPE_EGAC;			// EGA 彩色卡
			video_mem_end = 0xbc000;				// 使用内存镜像尾地址为 0xbc000
			display_desc = "EGAc";					// 显示器描述字符串 EGAc
		}
		// 等于 0x10 时为 CGA 彩色显卡
		else
		{
			video_type = VIDEO_TYPE_CGA;			// CGA 彩色卡
			video_mem_end = 0xba000;				// 使用内存镜像尾地址为 0xba000
			display_desc = "*CGA";					// 显示器描述字符串 *CGA
		}
	}

	/* Let the user known what kind of display driver we are using */
	// 使用直接将字符串写到显示内存的对应字节处的方法来定义屏幕右上角展示描述字符串；
	// 首先获取第一行距离右边界 8 个字节处指针
	display_ptr = ((char *)video_mem_start) + video_size_row - 8;
	// 循环复制显示器描述字符串，直到字符为空
	while (*display_desc)
	{
		*display_ptr++ = *display_desc++; 			// 复制字符
		display_ptr++; 								// 空出属性字节位置
	}
	
	/* Initialize the variables used for scrolling (mostly EGA/VGA)	*/
	// 初始化用与滚屏相关的变量
	origin	= video_mem_start; 						// 滚屏起始显示内存地址
	scr_end	= video_mem_start + video_num_lines * video_size_row; 	// 滚屏结束内存地址
	top	= 0; 										//最顶行哈
	bottom	= video_num_lines; 						//最底行号

	gotoxy(ORIG_X,ORIG_Y); 							//初始化光标位置 x，y 和对应的内存位置 pos
	set_trap_gate(0x21,&keyboard_interrupt); 		// 设置中断号为 0x21 键盘中断陷阱门
	outb_p(inb_p(0x21)&0xfd,0x21); 					// 取消 8259A 中对键盘中断的屏蔽，允许 IRQ1
	a=inb_p(0x61); 									// 延迟读取键盘端口 0x61
	outb_p(a|0x80,0x61); 							// 设置禁止键盘工作（位7置位）
	outb(a,0x61); 									// 在允许键盘工作，用于复位键盘操作
}
/* from bsd-net-2: */
/**
 * 关闭扬声器函数
*/
void sysbeepstop(void)
{
	/* disable counter 2 */
	outb(inb_p(0x61)&0xFC, 0x61); // 将控制器位 1 置为 0，关闭扬声器
}

int beepcount = 0; 									// 扬声器发声计数

/**
 * 蜂鸣函数
*/
static void sysbeep(void)
{
	/* enable counter 2 */
	outb_p(inb_p(0x61)|3, 0x61);
	/* set command for counter 2, 2 byte write */
	outb_p(0xB6, 0x43);
	/* send 0x637 for 750 HZ */
	outb_p(0x37, 0x42);
	outb(0x06, 0x42);
	/* 1/8 second */
	beepcount = HZ/8;	
}
