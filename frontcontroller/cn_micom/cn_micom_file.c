/*
 * cn_micom_file.c
 *
 * (c) 2009 Dagobert@teamducktales
 * (c) 2010 Schischu & konfetti: Add irq handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/termbits.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17)
#include <linux/stm/pio.h>
#else
#include <linux/stpio.h>
#endif
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/poll.h>

#include "cn_micom.h"
#include "cn_micom_asc.h"
#include "cn_micom_utf.h"
#include "cn_micom_char.h"

extern void ack_sem_up(void);
extern int  ack_sem_down(void);
int mcom_WriteString(unsigned char* aBuf, int len);
extern void mcom_putc(unsigned char data);

struct semaphore     write_sem;

int errorOccured = 0;

static unsigned char ioctl_data[8];

tFrontPanelOpen FrontPanelOpen [LASTMINOR];

struct saved_data_s
{
    int   length;
    char  data[128];
};

u8 regs[0xff];



enum {
	ICON_MIN,
	ICON_STANDBY,
	ICON_SAT,
	ICON_REC,
	ICON_TIMESHIFT,
	ICON_TIMER,
	ICON_HD,
	ICON_USB,
	ICON_SCRAMBLED,
	ICON_DOLBY,
	ICON_MUTE,
	ICON_TUNER1,
	ICON_TUNER2,
	ICON_MP3,
	ICON_REPEAT,
	ICON_Play,
	ICON_TER,
	ICON_FILE,
	ICON_480i,
	ICON_480p,
	ICON_576i,
	ICON_576p,
	ICON_720p,
	ICON_1080i,
	ICON_1080p,
	ICON_Play_1,
	ICON_MAX
};

struct iconToInternal {
	char *name;
	u16 icon;
	u8 internalCode1;
	u8 SymbolData1;
	u8 internalCode2;
	u8 SymbolData2;
} mcomIcons[] ={
/*--------------------- SetIcon ------- code1 data1 code2 data2 -----*/
	{ "ICON_STANDBY"  , ICON_STANDBY   , 0x20, 0x08, 0xff, 0x00}, // ok
	{ "ICON_SAT"      , ICON_SAT       , 0x20, 0x04, 0xff, 0x00}, // ok
	{ "ICON_REC"      , ICON_REC       , 0x30, 0x01, 0xff, 0x00}, // ok
	{ "ICON_TIMESHIFT", ICON_TIMESHIFT , 0x31, 0x02, 0x32, 0x02}, // ok
	{ "ICON_TIMER"    , ICON_TIMER     , 0x33, 0x02, 0xff, 0x00}, // ok
	{ "ICON_HD"       , ICON_HD        , 0x34, 0x02, 0xff, 0x00}, // ok
	{ "ICON_USB"      , ICON_USB       , 0x35, 0x02, 0xff, 0x00}, // ok
	{ "ICON_SCRAMBLED", ICON_SCRAMBLED , 0x36, 0x02, 0xff, 0x00}, // ok
	{ "ICON_DOLBY"    , ICON_DOLBY     , 0x37, 0x02, 0xff, 0x00}, // ok
	{ "ICON_MUTE"     , ICON_MUTE      , 0x38, 0x02, 0xff, 0x00}, // ok
	{ "ICON_TUNER1"   , ICON_TUNER1    , 0x39, 0x02, 0xff, 0x00}, // ok
	{ "ICON_TUNER2"   , ICON_TUNER2    , 0x3a, 0x02, 0xff, 0x00}, // ok
	{ "ICON_MP3"      , ICON_MP3       , 0x3b, 0x02, 0xff, 0x00}, // ok
	{ "ICON_REPEAT"   , ICON_REPEAT    , 0x3c, 0x02, 0xff, 0x00}, // ok
	{ "ICON_Play"     , ICON_Play      , 0x20, 0x01, 0xff, 0x00},
	{ "ICON_Play"     , ICON_Play      , 0x20, 0x02, 0xff, 0x00},
	{ "ICON_Play"     , ICON_Play      , 0x21, 0x01, 0xff, 0x00},
	{ "ICON_Play"     , ICON_Play      , 0x21, 0x02, 0xff, 0x00},
	{ "ICON_Play"     , ICON_Play      , 0x22, 0x01, 0xff, 0x00},
	{ "ICON_Play"     , ICON_Play      , 0x22, 0x02, 0xff, 0x00},
	{ "ICON_Play"     , ICON_Play      , 0x23, 0x01, 0xff, 0x00},
	{ "ICON_Play"     , ICON_Play      , 0x23, 0x02, 0xff, 0x00},
	{ "ICON_Play"     , ICON_Play      , 0x24, 0x01, 0xff, 0x00},
	{ "ICON_Play"     , ICON_Play      , 0x24, 0x02, 0xff, 0x00},
	{ "ICON_TER"      , ICON_TER       , 0x20, 0x04, 0xff, 0x00},
	{ "ICON_FILE"     , ICON_FILE      , 0x21, 0x04, 0xff, 0x00},
	{ "ICON_480i"     , ICON_480i      , 0x24, 0x40, 0x23, 0x40},
	{ "ICON_480p"     , ICON_480p      , 0x24, 0x40, 0x22, 0x40},
	{ "ICON_576i"     , ICON_576i      , 0x21, 0x40, 0x20, 0x40},
	{ "ICON_576p"     , ICON_576p      , 0x21, 0x40, 0x24, 0x20},
	{ "ICON_720p"     , ICON_720p      , 0x23, 0x20, 0xff, 0x00},
	{ "ICON_1080i"    , ICON_1080i     , 0x22, 0x20, 0xff, 0x00},
	{ "ICON_1080p"    , ICON_1080p     , 0x21, 0x20, 0xff, 0x00}
/* currently not used:
  0x36 0x01 -> ":" left between text
  0x39 0x01 -> ":" middle between text
  0x3a 0x01 -> ":" right between text

  0x23 0x4 ->a symbol ;)
  0x24 0x4 ->another symbol :D
 */
};

/* to the fp */
#define cCommandGetMJD           0x10
#define cCommandSetTimeFormat    0x11
#define cCommandSetMJD           0x15

#define cCommandSetPowerOff      0x20

#define cCommandPowerOffReplay   0x31
#define cCommandSetBootOn        0x40
#define cCommandSetBootOnExt     0x41

#define cCommandSetWakeupTime    0x72
#define cCommandSetWakeupMJD     0x74

#define cCommandGetPowerOnSrc    0x80

#define cCommandGetIrCode        0xa0
#define cCommandGetIrCodeExt     0xa1
#define cCommandSetIrCode        0xa5

#define cCommandGetPort          0xb2
#define cCommandSetPort          0xb3

#ifdef OCTAGON1008
#define cCommandSetIcon          0xc4
#else
#define cCommandSetIconI         0xc2 /* 0xc2 0xc7 0xcb 0xcc */ /* display cgram */
#define cCommandSetIconII        0xc7
#endif

#if defined(OCTAGON1008)
#define cCommandSetVFD           0xc4
#elif defined(HS7810A) || defined(HS7110) || defined(WHITEBOX)
#define cCommandSetVFD           0x24
#else
#define cCommandSetVFD           0xce /* 0xc0 */
#endif


#define cCommandSetVFDBrightness 0xd2

#if defined(ATEVIO7500) || defined(HS7810A) || defined(HS7110) || defined(WHITEBOX)
#define cCommandGetFrontInfo     0xd0
#else
#define cCommandGetFrontInfo     0xe0
#endif

#define cCommandSetPwrLed        0x93 /* added by zeroone, only used in this file; set PowerLed Brightness on HDBOX */

#define OW_TaskDelay(x)				msleep(x)
#if 0
#define PRN_LOG(msg, args...)		printk(msg, ##args)
#define PRN_DUMP(str, data, len)	mcom_Dump(str, data, len)
#else
#define PRN_LOG(msg, args...)
#define PRN_DUMP(str, data, len)
#endif

#define MAX_MCOM_MSG		30

#define MCU_COMM_PORT		UART_1
#define MCU_COMM_TIMEOUT	1000

#define _MCU_KEYIN		0xF1
#define _MCU_DISPLAY	0xED
#define _MCU_STANDBY	0xE5
#define _MCU_NOWTIME	0xEC
#define _MCU_WAKEUP		0xEA
#define _MCU_BOOT		0xEB
#define _MCU_TIME		0xFC
#define _MCU_RESPONSE	0xC5
#define _MCU_ICON		0xE1
#define _MCU_VERSION	0xDB
#define _MCU_REBOOT		0xE6
#define _MCU_PRIVATE	0xE7
#define _MCU_KEYCODE	0xE9

#define _TAG		0
#define _LEN		1
#define _VAL		2

enum
{
	FD_NULL,
	FD_DASH,

	FD_NUM_0,
	FD_NUM_1,
	FD_NUM_2,
	FD_NUM_3,
	FD_NUM_4,
	FD_NUM_5,
	FD_NUM_6,
	FD_NUM_7,
	FD_NUM_8,
	FD_NUM_9,

	FD_CH_A,
	FD_CH_B,
	FD_CH_C,
	FD_CH_D,
	FD_CH_E,
	FD_CH_F,
	FD_CH_G,
	FD_CH_H,
	FD_CH_I,
	FD_CH_J,
	FD_CH_K,
	FD_CH_L,
	FD_CH_M,
	FD_CH_N,
	FD_CH_O,
	FD_CH_P,
	FD_CH_Q,
	FD_CH_R,
	FD_CH_S,
	FD_CH_T,
	FD_CH_U,
	FD_CH_V,
	FD_CH_W,
	FD_CH_X,
	FD_CH_Y,
	FD_CH_Z,

#if (defined(_BUYER_SATCOM_RUSSIA) || defined(_BUYER_SOGNO_GERMANY)) && !defined(SUPPORT_MINILINE) && !defined(ST_7109)
	FD_CH_RUS_A,
    FD_CH_RUS_B,
    FD_CH_RUS_V,
    FD_CH_RUS_G,
    FD_CH_RUS_D,
    FD_CH_RUS_YE,
    FD_CH_RUS_YO,
	FD_CH_RUS_ZH,
	FD_CH_RUS_Z,
	FD_CH_RUS_I,
	FD_CH_RUS_EKRA,
	FD_CH_RUS_K,
	FD_CH_RUS_EL,
	FD_CH_RUS_M,
	FD_CH_RUS_N,
	FD_CH_RUS_O,
	FD_CH_RUS_P,
	FD_CH_RUS_R,
	FD_CH_RUS_S,
	FD_CH_RUS_T,
	FD_CH_RUS_U,
	FD_CH_RUS_F,
	FD_CH_RUS_KH,
	FD_CH_RUS_TS,
	FD_CH_RUS_CH,
	FD_CH_RUS_SH,
	FD_CH_RUS_SHCH,
	FD_CH_RUS_TVYOR,
	FD_CH_RUS_Y,
	FD_CH_RUS_MYAH,
	FD_CH_RUS_EOBEO,
	FD_CH_RUS_YU,
	FD_CH_RUS_YA,
#endif // END of #if defined(_BUYER_SATCOM_RUSSIA)

	FD_END
};

static unsigned int _1SEC  = 1000;
static unsigned int _100MS = 100;

static unsigned char		mcom_time[12];
static unsigned char		mcom_version[4];
static unsigned char		mcom_private[8]; // 0000 0000(Boot fron AC on), 0000 0001(Boot fron standby), 0000 0002(micom did not use this value, bug)

#if defined(SUPPORT_MINILINE)
static char	*mcom_std_char[] = {
				_14SEG_NULL, _14SEG_DASH,
				_14SEG_NUM_0, _14SEG_NUM_1, _14SEG_NUM_2, _14SEG_NUM_3, _14SEG_NUM_4, _14SEG_NUM_5, _14SEG_NUM_6, _14SEG_NUM_7,
				_14SEG_NUM_8, _14SEG_NUM_9,
				_14SEG_CH_A, _14SEG_CH_B, _14SEG_CH_C, _14SEG_CH_D, _14SEG_CH_E, _14SEG_CH_F, _14SEG_CH_G, _14SEG_CH_H, _14SEG_CH_I,
				_14SEG_CH_J, _14SEG_CH_K, _14SEG_CH_L, _14SEG_CH_M, _14SEG_CH_N, _14SEG_CH_O, _14SEG_CH_P, _14SEG_CH_Q, _14SEG_CH_R,
				_14SEG_CH_S, _14SEG_CH_T, _14SEG_CH_U, _14SEG_CH_V, _14SEG_CH_W, _14SEG_CH_X, _14SEG_CH_Y, _14SEG_CH_Z
		};
#else
static char	*mcom_std_char[] = {
				_14SEG_NULL, _14SEG_NULL,
				_14SEG_NUM_0, _14SEG_NUM_1, _14SEG_NUM_2, _14SEG_NUM_3, _14SEG_NUM_4, _14SEG_NUM_5, _14SEG_NUM_6, _14SEG_NUM_7,
				_14SEG_NUM_8, _14SEG_NUM_9,
				_14SEG_CH_A, _14SEG_CH_B, _14SEG_CH_C, _14SEG_CH_D, _14SEG_CH_E, _14SEG_CH_F, _14SEG_CH_G, _14SEG_CH_H, _14SEG_CH_I,
				_14SEG_CH_J, _14SEG_CH_K, _14SEG_CH_L, _14SEG_CH_M, _14SEG_CH_N, _14SEG_CH_O, _14SEG_CH_P, _14SEG_CH_Q, _14SEG_CH_R,
				_14SEG_CH_S, _14SEG_CH_T, _14SEG_CH_U, _14SEG_CH_V, _14SEG_CH_W, _14SEG_CH_X, _14SEG_CH_Y, _14SEG_CH_Z
		#if (defined(_BUYER_SATCOM_RUSSIA) || defined(_BUYER_SOGNO_GERMANY))&& !defined(ST_7109)
				, _14SEG_RUS_a, _14SEG_RUS_b, _14SEG_RUS_v, _14SEG_RUS_g, _14SEG_RUS_d, _14SEG_RUS_ye, _14SEG_RUS_yo,
				_14SEG_RUS_zh, _14SEG_RUS_z, _14SEG_RUS_i, _14SEG_RUS_ekra, _14SEG_RUS_k, _14SEG_RUS_el, _14SEG_RUS_m,
				_14SEG_RUS_n, _14SEG_RUS_o, _14SEG_RUS_p, _14SEG_RUS_r, _14SEG_RUS_s, _14SEG_RUS_t, _14SEG_RUS_u,
				_14SEG_RUS_f, _14SEG_RUS_kh, _14SEG_RUS_ts, _14SEG_RUS_ch, _14SEG_RUS_sh, _14SEG_RUS_shch, _14SEG_RUS_tvyor,
				_14SEG_RUS_y, _14SEG_RUS_myah, _14SEG_RUS_eobeo, _14SEG_RUS_yu, _14SEG_RUS_ya
		#endif // END of #if defined(_BUYER_SATCOM_RUSSIA)

		};
#endif

static struct saved_data_s lastdata;


static void mcom_Dump(char *string, unsigned char *v_pData, unsigned short v_szData)
{
	int i;
	char str[10], line[80], txt[25];
	unsigned char	*pData;

	if ((v_pData == NULL) || (v_szData == 0) )
	{
		return;
	}

	printk("[ %s [%x]]\n", string, v_szData);

	i=0;
	pData = v_pData;
	line[0] = '\0';
	while ( (unsigned short)(pData - v_pData) < v_szData)
	{
		sprintf(str, "%02x ", *pData);
		strcat(line,str);
		txt[i] = ((*pData >= 0x20) && (*pData < 0x7F))  ? *pData : '.';
		i++;

		if (i == 16)
		{
			txt[i] = '\0';
			printk("%s %s\n",line, txt);
			i = 0;
			line[0] = '\0';
		}
		pData++;
	}
	if (i != 0)
	{
		txt[i] = '\0';
		do
			strcat(line,"   ");
		while (++i < 16);

		printk("%s %s\n\n",line, txt);
	}
}

void write_sem_up(void)
{
    up(&write_sem);
}

int write_sem_down(void)
{
    return down_interruptible (&write_sem);
}

void copyData(unsigned char* data, int len)
{
    printk("%s len %d\n", __func__, len);
    memcpy(ioctl_data, data, len);
}

int mcomWriteCommand(char* buffer, int len, int needAck)
{
    int i;

    dprintk(150, "%s >\n", __func__);

    for (i = 0; i < len; i++)
    {
#ifdef DIRECT_ASC
          serial_putc (buffer[i]);
#else
          mcom_putc(buffer[i]);
#endif
    }

    if (needAck)
        if (ack_sem_down())
            return -ERESTARTSYS;

    dprintk(150, "%s < \n", __func__);

    return 0;
}

int mcomSetIcon(int which, int on)
{
	return 0;
}

/* export for later use in e2_proc */
EXPORT_SYMBOL(mcomSetIcon);

int mcomSetLED(int which, int on)
{
    char buffer[8];
    int res = 0;

    dprintk(100, "%s > %d, %d\n", __func__, which, on);

//FIXME
    if (which < 1 || which > 6)
    {
        printk("VFD/CNMicom led number out of range %d\n", which);
        return -EINVAL;
    }

    memset(buffer, 0, 8);

//fixme

    dprintk(100, "%s <\n", __func__);

    return res;
}

/* export for later use in e2_proc */
EXPORT_SYMBOL(mcomSetLED);

int mcomSetBrightness(int level)
{
    char buffer[8];
    int res = 0;

    dprintk(100, "%s > %d\n", __func__, level);

#if !defined(HS7810A) && !defined(HS7110) && !defined(WHITEBOX)
    if (level < 0 || level > 7)
    {
        printk("VFD/CNMicom brightness out of range %d\n", level);
        return -EINVAL;
    }

    memset(buffer, 0, 8);

    buffer[0] = SOP;
    buffer[1] = cCommandSetVFDBrightness;
    buffer[2] = 0x00;

    buffer[3] = level;
    buffer[4] = EOP;

    res = mcomWriteCommand(buffer, 5, 0);
#endif

    dprintk(100, "%s <\n", __func__);

    return res;
}
/* export for later use in e2_proc */
EXPORT_SYMBOL(mcomSetBrightness);

int mcomSetPwrLed(int level)
{
    char buffer[8];
    int res = 0;

    dprintk(100, "%s > %d\n", __func__, level);

#if !defined(HS7810A) && !defined(HS7110) && !defined(WHITEBOX)
    if (level < 0 || level > 15)
    {
        printk("VFD/CNMicom PwrLed out of range %d\n", level);
        return -EINVAL;
    }

    memset(buffer, 0, 8);

    buffer[0] = SOP;
    buffer[1] = cCommandSetPwrLed;
    buffer[2] = 0xf2;
    buffer[3] = level;
    buffer[4] = 0x00;
    buffer[5] = EOP;

    res = mcomWriteCommand(buffer, 6, 0);
#endif

    dprintk(100, "%s <\n", __func__);

    return res;
}
/* export for later use in e2_proc */
EXPORT_SYMBOL(mcomSetPwrLed);

int mcomSetStandby(char* time)
{
    char     buffer[8];
    char     power_off[] = {SOP, cCommandPowerOffReplay, 0x01, EOP};
    int      res = 0;

    dprintk(100, "%s >\n", __func__);

    res = mcom_WriteString("Bye bye ...", strlen("Bye bye ..."));

    /* set wakeup time */
    memset(buffer, 0, 8);

    buffer[0] = SOP;
    buffer[1] = cCommandSetWakeupMJD;

    memcpy(buffer + 2, time, 4); /* only 4 because we do not have seconds here */
    buffer[6] = EOP;

    res = mcomWriteCommand(buffer, 7, 0);

    /* now power off */
    res = mcomWriteCommand(power_off, sizeof(power_off), 0);

    dprintk(100, "%s <\n", __func__);

    return res;
}

int mcomSetTime(char* time)
{
    char       buffer[8];
    int      res = 0;

    dprintk(100, "%s >\n", __func__);

    memset(buffer, 0, 8);

    buffer[0] = SOP;
    buffer[1] = cCommandSetMJD;

    memcpy(buffer + 2, time, 5);
    buffer[7] = EOP;

    res = mcomWriteCommand(buffer, 8, 0);

    dprintk(100, "%s <\n", __func__);

    return res;
}

int mcomGetTime(void)
{
    char       buffer[3];
    int      res = 0;

    dprintk(100, "%s >\n", __func__);

    memset(buffer, 0, 3);

    buffer[0] = SOP;
    buffer[1] = cCommandGetMJD;
    buffer[2] = EOP;

    errorOccured = 0;
    res = mcomWriteCommand(buffer, 3, 1);

    if (errorOccured == 1)
    {
        /* error */
        memset(ioctl_data, 0, 8);
        printk("error\n");
        res = -ETIMEDOUT;
    } else
    {
        /* time received ->noop here */
        dprintk(1, "time received\n");
        dprintk(20, "myTime= 0x%02x - 0x%02x - 0x%02x - 0x%02x - 0x%02x\n", ioctl_data[0], ioctl_data[1]
                , ioctl_data[2], ioctl_data[3], ioctl_data[4]);
    }

    dprintk(100, "%s <\n", __func__);

    return res;
}

int mcomGetWakeUpMode(void)
{
    char       buffer[8];
    int      res = 0;

    dprintk(100, "%s >\n", __func__);

    memset(buffer, 0, 8);

    buffer[0] = SOP;
    buffer[1] = cCommandGetPowerOnSrc;
    buffer[2] = EOP;

    errorOccured = 0;
    res = mcomWriteCommand(buffer, 3, 1);

    if (errorOccured == 1)
    {
        /* error */
        memset(ioctl_data, 0, 8);
        printk("error\n");

        res = -ETIMEDOUT;
    } else
    {
        /* time received ->noop here */
        dprintk(1, "time received\n");
    }

    dprintk(100, "%s <\n", __func__);

    return res;
}

int mcom_WriteString(unsigned char* aBuf, int len)
{
    unsigned char bBuf[128];
    int i =0;
    int j =0;
    int payload_len;
    int res = 0;

    dprintk(100, "%s >\n", __func__);
    printk("\n\n\n\n\n%s -> !!!!!!!!!! \n\n\n\n\n\n\n", aBuf);

    memset(bBuf, ' ', 128);

    for (i = 0, payload_len = 0; i < len; i++)
    {
        memcpy(bBuf + _VAL + payload_len, mcom_std_char[aBuf[i]], 2);
        payload_len += 2;
    }

    /* start of command write */
    bBuf[0] = _MCU_DISPLAY;
    bBuf[1] = payload_len;

    /* save last string written to fp */
    memcpy(&lastdata.data, aBuf, 128);
    lastdata.length = len;

    dprintk(70, "len %d\n", len);

    res = mcomWriteCommand(bBuf, payload_len, 0);

    dprintk(100, "%s <\n", __func__);

    return res;
}


static unsigned char mcom_Checksum(unsigned char *payload, int len)
{
	int					n;
	unsigned char		checksum = 0;


	for (n = 0; n < len; n++)
		checksum ^= payload[n];

	return checksum;
}

static int mcom_SendResponse(char *buf, int needack)
{
	unsigned char	response[3];
	int				res = 0;

	response[_TAG] = _MCU_RESPONSE;
	response[_LEN] = 1;
	response[_VAL] = mcom_Checksum(buf, 2/*tag+len*/ + buf[_LEN]);

	if (needack)
	{
		errorOccured = 0;
		res = mcomWriteCommand((char*)response, 3, 1);

		if (errorOccured == 1)
		{
			/* error */
			memset(ioctl_data, 0, 8);
			printk("error\n");

	        res = -ETIMEDOUT;
		}

		return res;
	}
	else
	{
		mcomWriteCommand((char*)response, 3, 0);

		return 0;
	}
}

int mcom_init_func(void)
{
	unsigned char		comm_buf[20];
	unsigned char		n,k;
	unsigned char		checksum;
	int					res;
	int					bootSendCount;
#if !defined(SUPPORT_MINILINE)
	int					seq_count = -1;
#endif


	dprintk(100, "%s >\n", __func__);

	sema_init(&write_sem, 1);

MCOM_RECOVER :


#if 0
#if !defined(SUPPORT_MINILINE)
	seq_count++;
	if(seq_count > 0)
	{
		mcom_FlushData();
		if((seq_count % 10) == 0)
		{
			mcom_SetupRetryDisplay(seq_count);
			OW_TaskDelay(_1SEC * 2);
		}
	}
#endif
#endif
	bootSendCount = 0;
	while(1)
	{
		// send "BOOT"
		comm_buf[_TAG] = _MCU_BOOT;
		comm_buf[_LEN] = n = 1;
		comm_buf[_VAL] = 0x01;	// dummy

		errorOccured = 0;
		PRN_DUMP("## SEND:BOOT ##", comm_buf, 2/*tag+len*/ + n);
		res = mcomWriteCommand(comm_buf, 3, 1);

		if (errorOccured == 1)
		{
			/* error */
			memset(ioctl_data, 0, 8);
			printk("error\n");

			OW_TaskDelay(_100MS);
			goto MCOM_RECOVER;
		}
		else
		{
			/* time received ->noop here */
			dprintk(1, "time received\n");
			//printk("received\n");
		}

		// recv "VERSION" or "TIME"
		PRN_DUMP("## RECV:VERSION or TIME ", ioctl_data, 2/*tag+len*/ + ioctl_data[_LEN]);

		if (ioctl_data[_TAG] == _MCU_VERSION)
		{
			memcpy(mcom_version, ioctl_data + _VAL, ioctl_data[_LEN]);
			//printk("_MCU_VERSION\n");
			break;
		}
		else if (ioctl_data[_TAG] == _MCU_TIME)
		{
			memcpy(mcom_time, ioctl_data + _VAL, ioctl_data[_LEN]);
			//printk("_MCU_TIME\n");
			break;
		}

		//printk("tag = %02x\n", ioctl_data[_TAG]);

		if (bootSendCount++ == 3)
			goto MCOM_RECOVER;

		OW_TaskDelay(_100MS);
	}

	// send "RESPONSE" and recv "TIME" or "VERSION
	res = mcom_SendResponse(ioctl_data, 1);
	if (res)
	{
		OW_TaskDelay(_100MS);
		goto MCOM_RECOVER;
	}

	PRN_DUMP("## RECV:TIME or VERSION ", ioctl_data, ioctl_data[_LEN] + 2);
	if (ioctl_data[_TAG] == _MCU_TIME)
	{
		memcpy(mcom_time, ioctl_data + _VAL, ioctl_data[_LEN]);
	}
	else if(ioctl_data[_TAG] == _MCU_VERSION)
	{
		memcpy(mcom_version, ioctl_data + _VAL, ioctl_data[_LEN]);
	}
	else
	{
		OW_TaskDelay(_100MS);
		goto MCOM_RECOVER;
	}

//	PRN_LOG("mcutype[%d]\n", MCOM_GetMcuType());
	if(1)	//(MCOM_GetMcuType() > _MCU_TYPE_1)
	{
		// recv "PRIVATE"
		res = mcom_SendResponse(ioctl_data, 1);
		if (res)
		{
			OW_TaskDelay(_100MS);
			goto MCOM_RECOVER;
		}

		PRN_DUMP("## RECV:PRIVATE ##", ioctl_data, ioctl_data[_LEN] + 2);
		memcpy(mcom_private, ioctl_data + _VAL, ioctl_data[_LEN]);

		mcom_SendResponse(ioctl_data, 0);

		OW_TaskDelay(_100MS);
		for(k = 0; k < 3; k++)
		{
			// send "KEYCODE"
			comm_buf[_TAG] = _MCU_KEYCODE;
			comm_buf[_LEN] = n = 4;
			comm_buf[_VAL + 0] = 0x04;
			comm_buf[_VAL + 1] = 0xF3;
			comm_buf[_VAL + 2] = 0x5F;
			comm_buf[_VAL + 3] = 0xA0;

			checksum = mcom_Checksum(comm_buf, 6);

			errorOccured = 0;
			PRN_DUMP("## SEND:KEYCODE ##", comm_buf, 2/*tag+len*/ + n);
			res = mcomWriteCommand(comm_buf, 6, 1);

			if (errorOccured == 1)
			{
				/* error */
				memset(ioctl_data, 0, 8);
				printk("error\n");

				OW_TaskDelay(_100MS);
				goto MCOM_RECOVER;
			}

			if(res == 0)
			{
				if(checksum == ioctl_data[_VAL])
					break;
			}
		}

		if(k >= 3)
		{
			OW_TaskDelay(_100MS);
			goto MCOM_RECOVER;
		}

	}

    dprintk(100, "%s <\n", __func__);

	return 0;
}

static ssize_t MCOMdev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
    char* kernel_buf;
    int minor, vLoop, res = 0;

    dprintk(100, "%s > (len %d, offs %d)\n", __func__, len, (int) *off);
    //printk("%s > (len %d, offs %d)\n", __func__, len, (int) *off);

    if (len == 0)
        return len;

    minor = -1;
    for (vLoop = 0; vLoop < LASTMINOR; vLoop++)
    {
        if (FrontPanelOpen[vLoop].fp == filp)
        {
            minor = vLoop;
        }
    }

    if (minor == -1)
    {
        printk("Error Bad Minor\n");
        return -1; //FIXME
    }

    dprintk(70, "minor = %d\n", minor);

    /* dont write to the remote control */
    if (minor == FRONTPANEL_MINOR_RC)
        return -EOPNOTSUPP;

    kernel_buf = kmalloc(len, GFP_KERNEL);

    if (kernel_buf == NULL)
    {
        printk("%s return no mem<\n", __func__);
        return -ENOMEM;
    }

    copy_from_user(kernel_buf, buff, len);

    if (write_sem_down())
        return -ERESTARTSYS;

    /* Dagobert: echo add a \n which will be counted as a char
     */
    if (kernel_buf[len - 1] == '\n')
        res = mcom_WriteString(kernel_buf, len - 1);
    else
        res = mcom_WriteString(kernel_buf, len);

    kfree(kernel_buf);

    write_sem_up();

    dprintk(70, "%s < res %d len %d\n", __func__, res, len);

    if (res < 0)
        return res;
    else
        return len;
}

static ssize_t MCOMdev_read(struct file *filp, char __user *buff, size_t len, loff_t *off)
{
    int minor, vLoop;

    //printk("%s > (len %d, offs %d)\n", __func__, len, (int) *off);
    dprintk(100, "%s > (len %d, offs %d)\n", __func__, len, (int) *off);

    if (len == 0)
        return len;

    minor = -1;
    for (vLoop = 0; vLoop < LASTMINOR; vLoop++)
    {
        if (FrontPanelOpen[vLoop].fp == filp)
        {
            minor = vLoop;
        }
    }

    if (minor == -1)
    {
        printk("Error Bad Minor\n");
        return -EUSERS;
    }

    dprintk(100, "minor = %d\n", minor);
    //printk("minor = %d\n", minor);

    if (minor == FRONTPANEL_MINOR_RC)
    {
        int           size = 0;
        unsigned char data[20];

        memset(data, 0, 20);

        getRCData(data, &size);

        if (size > 0)
        {
            if (down_interruptible(&FrontPanelOpen[minor].sem))
                return -ERESTARTSYS;

            copy_to_user(buff, data, size);

            up(&FrontPanelOpen[minor].sem);

            dprintk(100, "%s < %d\n", __func__, size);
            return size;
        }

	    //printk("size = %d\n", size);
        dumpValues();

        return 0;
    }

    /* copy the current display string to the user */
    if (down_interruptible(&FrontPanelOpen[minor].sem))
    {
        printk("%s return erestartsys<\n", __func__);
        return -ERESTARTSYS;
    }

    if (FrontPanelOpen[minor].read == lastdata.length)
    {
        FrontPanelOpen[minor].read = 0;

        up (&FrontPanelOpen[minor].sem);
        printk("%s return 0<\n", __func__);
        return 0;
    }

    if (len > lastdata.length)
        len = lastdata.length;

    /* fixme: needs revision because of utf8! */
    if (len > 16)
        len = 16;

    FrontPanelOpen[minor].read = len;
    copy_to_user(buff, lastdata.data, len);

    up (&FrontPanelOpen[minor].sem);

    dprintk(100, "%s < (len %d)\n", __func__, len);
    return len;
}

int MCOMdev_open(struct inode *inode, struct file *filp)
{
    int minor;

    dprintk(100, "%s >\n", __func__);

    /* needed! otherwise a racecondition can occur */
    if(down_interruptible (&write_sem))
        return -ERESTARTSYS;

    minor = MINOR(inode->i_rdev);

    dprintk(70, "open minor %d\n", minor);

    if (FrontPanelOpen[minor].fp != NULL)
    {
        printk("EUSER\n");
        up(&write_sem);
        return -EUSERS;
    }

    FrontPanelOpen[minor].fp = filp;
    FrontPanelOpen[minor].read = 0;

    up(&write_sem);

    dprintk(100, "%s <\n", __func__);

    return 0;

}

int MCOMdev_close(struct inode *inode, struct file *filp)
{
    int minor;

    dprintk(100, "%s >\n", __func__);

    minor = MINOR(inode->i_rdev);

    dprintk(20, "close minor %d\n", minor);

    if (FrontPanelOpen[minor].fp == NULL)
    {
        printk("EUSER\n");
        return -EUSERS;
    }
    FrontPanelOpen[minor].fp = NULL;
    FrontPanelOpen[minor].read = 0;

    dprintk(100, "%s <\n", __func__);
    return 0;
}

static int MCOMdev_ioctl(struct inode *Inode, struct file *File, unsigned int cmd, unsigned long arg)
{
    static int mode = 0;
    struct cn_micom_ioctl_data * mcom = (struct cn_micom_ioctl_data *)arg;
    int res = 0;

	return -1;

    dprintk(100, "%s > 0x%.8x\n", __func__, cmd);

    if(down_interruptible (&write_sem))
        return -ERESTARTSYS;

    switch(cmd) {
    case VFDSETMODE:
        mode = mcom->u.mode.compat;
        break;
    case VFDSETLED:
        res = mcomSetLED(mcom->u.led.led_nr, mcom->u.led.on);
        break;
    case VFDBRIGHTNESS:
        if (mode == 0)
        {
            struct vfd_ioctl_data *data = (struct vfd_ioctl_data *) arg;
            res = mcomSetBrightness(data->start_address);
        } else
        {
            res = mcomSetBrightness(mcom->u.brightness.level);
        }
        mode = 0;
        break;
    case VFDPWRLED:
        if (mode == 0)
        {
            struct vfd_ioctl_data *data = (struct vfd_ioctl_data *) arg;
            res = mcomSetPwrLed(data->start_address);
        } else
        {
            res = mcomSetPwrLed(mcom->u.pwrled.level);
        }
        mode = 0;
        break;
    case VFDDRIVERINIT:
        res = mcom_init_func();
        mode = 0;
        break;
    case VFDICONDISPLAYONOFF:
#ifndef ATEVIO7500
        if (mode == 0)
        {
            struct vfd_ioctl_data *data = (struct vfd_ioctl_data *) arg;
            int icon_nr = (data->data[0] & 0xf) + 1;
            int on = data->data[4];
            res = mcomSetIcon(icon_nr, on);
        } else
        {
            res = mcomSetIcon(mcom->u.icon.icon_nr, mcom->u.icon.on);
        }
#else
        res = 0;
#endif
        mode = 0;
        break;
    case VFDSTANDBY:
        res = mcomSetStandby(mcom->u.standby.time);
        break;
    case VFDSETTIME:
        if (mcom->u.time.time != 0)
            res = mcomSetTime(mcom->u.time.time);
        break;
    case VFDGETTIME:
        res = mcomGetTime();
        copy_to_user(arg, &ioctl_data, 5);
        break;
    case VFDGETWAKEUPMODE:
        res = mcomGetWakeUpMode();
        copy_to_user(arg, &ioctl_data, 1);
        break;
    case VFDDISPLAYCHARS:
        if (mode == 0)
        {
            struct vfd_ioctl_data *data = (struct vfd_ioctl_data *) arg;
            res = mcom_WriteString(data->data, data->length);
        } else
        {
            //not suppoerted
        }
        mode = 0;
        break;
    case VFDDISPLAYWRITEONOFF:
        /* ->alles abschalten ? VFD_Display_Write_On_Off */
        printk("VFDDISPLAYWRITEONOFF ->not yet implemented\n");
        break;
    case 0x5401:
	mode = 0;
	break;
    default:
        printk("VFD/CNMicom: unknown IOCTL 0x%x\n", cmd);
        mode = 0;
        break;
    }
    up(&write_sem);
    dprintk(100, "%s <\n", __func__);
    return res;
}

struct file_operations vfd_fops =
{
    .owner = THIS_MODULE,
    .ioctl = MCOMdev_ioctl,
    .write = MCOMdev_write,
    .read  = MCOMdev_read,
    .open  = MCOMdev_open,
    .release  = MCOMdev_close
};
