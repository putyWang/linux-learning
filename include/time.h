#ifndef _TIME_H
#define _TIME_H

#ifndef _TIME_T
#define _TIME_T
typedef long time_t; // 从 GMT 1970-01-01 开始的以秒计数的时间（日历时间）
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#define CLOCKS_PER_SEC 100 // 系统时钟嘀嗒频率，100Hz

typedef long clock_t; // 从进程开始系统经过的时间滴答数

/*
	内核时间结构
*/
struct tm {
	int tm_sec; // 秒
	int tm_min; // 分
	int tm_hour; // 时
	int tm_mday; // 一个月中的天数
	int tm_mon; // 月份
	int tm_year; // 1900 年开始的年数
	int tm_wday; // 一星期中的某天 [0,6]（星期天 = 0）
	int tm_yday; // 一年中的某天
	int tm_isdst; // 夏令时标记
};

clock_t clock(void);
time_t time(time_t * tp);
double difftime(time_t time2, time_t time1);
time_t mktime(struct tm * tp);

char * asctime(const struct tm * tp);
char * ctime(const time_t * tp);
struct tm * gmtime(const time_t *tp);
struct tm *localtime(const time_t * tp);
size_t strftime(char * s, size_t smax, const char * fmt, const struct tm * tp);
void tzset(void);

#endif
