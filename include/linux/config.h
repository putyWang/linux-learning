#ifndef _CONFIG_H
#define _CONFIG_H

/*
 * The root-device is no longer hard-coded. You can change the default
 * root-device by changing the line ROOT_DEV = XXX in boot/bootsect.s
 */
// 可以通过修改 boot/bootsect.s 中的 ROOT_DEV = XXX 修改根文件设备的默认设置值

/*
 * define your keyboard here -
 * KBD_FINNISH for Finnish keyboards
 * KBD_US for US-type
 * KBD_GR for German keyboards
 * KBD_FR for Frech keyboard
 */
// 定义键盘类型
/*#define KBD_US */ // 美式键盘
/*#define KBD_GR */ // 德式键盘
/*#define KBD_FR */ // 法式键盘
#define KBD_FINNISH // 默认使用芬兰键盘

/*
 * Normally, Linux can get the drive parameters from the BIOS at
 * startup, but if this for some unfathomable reason fails, you'd
 * be left stranded. For this case, you can define HD_TYPE, which
 * contains all necessary info on your harddisk.
 *
 * The HD_TYPE macro should look like this:
 *
 * #define HD_TYPE { head, sect, cyl, wpcom, lzone, ctl}
 *
 * In case of two harddisks, the info should be sepatated by
 * commas:
 *
 * #define HD_TYPE { h,s,c,wpcom,lz,ctl },{ h,s,c,wpcom,lz,ctl }
 */
/**
 * 通常情况，在setup.s 中从BIOS 中能获取到硬盘驱动参数，
 * 在未获取到硬盘参数时可以通过定义 HD_TYPE 来定义硬盘参数表
 * 两个硬盘参数之间使用 ，号隔开
*/
/*
 This is an example, two drives, first is type 2, second is type 3:

#define HD_TYPE { 4,17,615,300,615,8 }, { 6,17,615,300,615,0 } // 硬盘定义的例子

 NOTE: ctl is 0 for all drives with heads<=8, and ctl=8 for drives
 with more than 8 heads.

 If you want the BIOS to tell what kind of drive you have, just
 leave HD_TYPE undefined. This is the normal thing to do.
*/

#endif
