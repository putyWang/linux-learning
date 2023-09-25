/*
 *  linux/kernel/mktime.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <time.h>

/*
 * This isn't the library routine, it is only used in the kernel.
 * as such, we don't care about years<1970 etc, but assume everything
 * is ok. Similarly, TZ etc is happily ignored. We just do everything
 * as easily as possible. Let's find something public for the library
 * routines (although I think minix times is public).
 */
/*
 * PS. I hate whoever though up the year 1970 - couldn't they have gotten
 * a leap-year instead? I also hate Gregorius, pope or no. I'm grumpy.
 */
#define MINUTE 60			// 1分钟的秒数
#define HOUR (60*MINUTE)	// 1小时的秒数
#define DAY (24*HOUR)		// 1天的秒数
#define YEAR (365*DAY)		// 一年的秒数

// 一年为界限，定义了每个月开始时对的秒数时间数组
static int month[12] = {
	0,
	DAY*(31),
	DAY*(31+29),
	DAY*(31+29+31),
	DAY*(31+29+31+30),
	DAY*(31+29+31+30+31),
	DAY*(31+29+31+30+31+30),
	DAY*(31+29+31+30+31+30+31),
	DAY*(31+29+31+30+31+30+31+31),
	DAY*(31+29+31+30+31+30+31+31+30),
	DAY*(31+29+31+30+31+30+31+31+30+31),
	DAY*(31+29+31+30+31+30+31+31+30+31+30)
};

/**
 * 计算 1970年1月1日0时起到开机当日经过的秒数，作为开机时间
 * @param tm 当前内核时间
 * @return 1970年1月1日0时起到开机当日经过的秒数
*/
long kernel_mktime(struct tm * tm)
{
	long res;
	int year;

	year = tm->tm_year - 70; 			// 从 1970 年到现在的年数
/* magic offsets (y+1) needed to get leapyears right.*/
	res = YEAR*year + DAY*((year+1)/4); // 这个年经过的秒数，每个闰年多一天
	res += month[tm->tm_mon]; 			// 加上今年经过月份秒数
/* and (y+2) here. If it wasn't a leap-year, we have to adjust */
	if (tm->tm_mon>1 && ((year+2)%4))
		res -= DAY;
	res += DAY*(tm->tm_mday-1); 		// 加上该月过的天数秒数
	res += HOUR*tm->tm_hour;			// 加上小时
	res += MINUTE*tm->tm_min;			// 加上分钟
	res += tm->tm_sec;					// 加上秒数
	return res;
}
