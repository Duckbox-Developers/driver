/*
 * aotom_vfd.c
 *
 * (c) 2010 Spider-Team
 * (c) 2011 oSaoYa
 * (c) 2012-2013 martii
 * (c) 2013-2014 Audioniek
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
 ****************************************************************************
 *
 * Changes
 *
 * Date     By              Description
 * --------------------------------------------------------------------------
 * 20130904 Audioniek       Brightness control fixed on Spark7162.
 * 20130906 Audioniek       Spark7162 character map expanded to include
 *                          all printable characters including lower case,
 *                          VFD Character translation table made redundant.
 * 20140603 Audioniek       LED lower case changed, more characters added.
 * 20140604 Audioniek       DVFD panel support added.
 * 20140612 Audioniek       UTF8 support added (thnx martii).
 * 20141125 Audioniek       Uppercase only letters on spark7162 (D)VFD made
 *                          switchable through #define LOWER_CASE (in
 *                          aotom_main.h).
 *
 ****************************************************************************/

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/stm/pio.h>
#include <linux/input.h>

#include "aotom_i2csoft.h"
#include "aotom_trace.h"
#include "aotom_main.h"

#include "utf.h"

YWFP_INFO_t YWFP_INFO;

//#define DEBUG //TODO: unify debug output
#ifdef DEBUG
	#ifdef MODULE
		#define YWVFD_Debug(x...) printk(x)
	#else
		#define YWVFD_Debug(x...) printf(x)
	#endif
#else
	#define YWVFD_Debug(x...)
#endif

/****************************************************************************/

static SegAddrVal_T VfdSegAddr[15];
struct semaphore vfd_sem;
struct semaphore vfd_sem_rw;
struct rw_semaphore vfd_rws;
static YWPANEL_FP_DispType_t panel_disp_type = YWPANEL_FP_DISPTYPE_UNKNOWN;

static struct stpio_pin *pio_scl;
static struct stpio_pin *pio_sda;
static struct stpio_pin *pio_cs;

typedef enum PIO_Mode_e
{
	PIO_Out,
	PIO_In
} PIO_Mode_T;

// Segment table 1 for VFD clock display on clock part (standby)
static u8 YWPANEL_CharArray[]=
//
//  aaaaaaa 
// f       b
// f       b
// f       b
//  ggggggg
// e       c
// e       c
// e       c
//  ddddddd  
//
{	         //  dabfgec
	0x7B,    // 01111011 0
	0x11,    // 00010001 1
	0x76,    // 01110110 2
	0x75,    // 01110101 3
	0x1D,    // 00011101 4
	0x6D,    // 01101101 5
	0x6F,    // 01101111 6
	0x31,    // 00110001 7
	0xFF,    // 11111111 8
	0x7D     // 01111101 9
};

//
// Charlib: Segment table for VFD text display (Spark7162)
//
//   aaaaaaa
//  fh  j  kb
//  f h j k b
//  f  hjk  b
//   gggimmm
//  e  rpn  c
//  e r p n c
//  er  p  nc
//   ddddddd
//
/*               7 6 5 4 3 2 1 0 */
/*address 0 8bit g i m c r p n e */
/*address 1 7bit   d a b f k j h */
//
// segment a = A1  32 0x20
// segment b = A1  16 0x10
// segment c = A0  16 0x10
// segment d = A1  64 0x40
// segment e = A0   1 0x01
// segment f = A1   8 0x08
// segment g = A0 128 0x80
// segment h = A1   1 0x01
// segment i = A0  64 0x40
// segment j = A1   2 0x02
// segment k = A1   4 0x04
// segment m = A0  32 0x20
// segment n = A0   2 0x02
// segment p = A0   4 0x04
// segment r = A0   8 0x08

static u8 CharLib[0x60][2] =
{	// A0    A1
	{0x00, 0x00},	// space
	{0x04, 0x02},	// !
	{0x00, 0x18},	// "
	{0xF1, 0x40},	// #
	{0xF4, 0x6A},	// $
	{0x58, 0x0C},	// %
	{0xC3, 0x49},	// &
	{0x00, 0x04},	// '
	{0x42, 0x04},	// (
	{0x48, 0x01},	// )
	{0xEE, 0x07},	// *
	{0xE4, 0x02},	// +
	{0x14, 0x00},	// ,
	{0xE0, 0x00},	// -
	{0x32, 0x00},	// .
	{0x48, 0x04},	// /

	{0x11, 0x78},	// 0
	{0x10, 0x14},	// 1
	{0xE1, 0x70},	// 2
	{0xF0, 0x70},	// 3
	{0xF0, 0x18},	// 4
	{0xF0, 0x68},	// 5
	{0xF1, 0x68},	// 6
	{0x10, 0x30},	// 7
	{0xF1, 0x78},	// 8
	{0xF0, 0x78},	// 9
	{0x04, 0x02},	// :
	{0x08, 0x02},	// ;
	{0x02, 0x04},	// <
	{0xE0, 0x40},	// =
	{0x08, 0x01},	// >
	{0x64, 0x30},	// ?

	{0xE1, 0x78},	// @
	{0xF1, 0x38},	// A
	{0x74, 0x72},	// B
	{0x01, 0x68},	// C
	{0x54, 0x72},	// D
	{0xE1, 0x68},	// E
	{0xE1, 0x28},	// F
	{0x71, 0x68},	// G
	{0xF1, 0x18},	// H
	{0x44, 0x62},	// I
	{0x11, 0x70},	// J
	{0xC3, 0x0C},	// K
	{0x01, 0x48},	// L
	{0x51, 0x1D},	// M
	{0x53, 0x19},	// N
	{0x11, 0x78},	// O

	{0xE1, 0x38},	// P
	{0x13, 0x78},	// Q
	{0xE3, 0x38},	// R
	{0xF0, 0x68},	// S
	{0x44, 0x22},	// T
	{0x11, 0x58},	// U
	{0x49, 0x0C},	// V
	{0x5B, 0x18},	// W
	{0x4A, 0x05},	// X
	{0x44, 0x05},	// Y
	{0x48, 0x64},	// Z
	{0x01, 0x68},	// [	
	{0x42, 0x01},	// backslash
	{0x10, 0x70},	// ]	
	{0x4A, 0x00},	// ^
	{0x00, 0x40},	// _

#if defined(LOWER_CASE)
	{0x00, 0x01},	// `
	{0x78, 0x70},	// a
	{0xF1, 0x48},	// b
	{0xE1, 0x40},	// c
	{0xF1, 0x50},	// d
	{0xE1, 0x78},	// e
	{0xC1, 0x28},	// f
	{0xF0, 0x78},	// g
	{0xF1, 0x08},	// h
	{0x44, 0x00},	// i
	{0x10, 0x50},	// j
	{0xE3, 0x08},	// k
	{0x44, 0x02},	// l
	{0xB5, 0x00},	// m
	{0xF1, 0x00},	// n
	{0xF1, 0x40},	// o

	{0xE1, 0x38},	// p
	{0xF0, 0x38},	// q
	{0xE1, 0x00},	// r
	{0xF0, 0x68},	// s
	{0xC1, 0x48},	// t
	{0x11, 0x40},	// u
	{0x09, 0x00},	// v
	{0x15, 0x40},	// w
	{0x4A, 0x05},	// x
	{0xF0, 0x58},	// y
	{0x48, 0x64},	// z
	{0x42, 0x04},	// {
	{0x04, 0x02},	// |
	{0x48, 0x01},	// }
	{0x00, 0x00}    //DEL (or end of table l=0x60)
#else
	{0x00, 0x01},	// `
	{0xF1, 0x38},	// A
	{0x74, 0x72},	// B
	{0x01, 0x68},	// C
	{0x54, 0x72},	// D
	{0xE1, 0x68},	// E
	{0xE1, 0x28},	// F
	{0x71, 0x68},	// G
	{0xF1, 0x18},	// H
	{0x44, 0x62},	// I
	{0x11, 0x70},	// J
	{0xC3, 0x0C},	// K
	{0x01, 0x48},	// L
	{0x51, 0x1D},	// M
	{0x53, 0x19},	// N
	{0x11, 0x78},	// O

	{0xE1, 0x38},	// P
	{0x13, 0x78},	// Q
	{0xE3, 0x38},	// R
	{0xF0, 0x68},	// S
	{0x44, 0x22},	// T
	{0x11, 0x58},	// U
	{0x49, 0x0C},	// V
	{0x5B, 0x18},	// W
	{0x4A, 0x05},	// X
	{0x44, 0x05},	// Y
	{0x48, 0x64},	// Z
	{0x42, 0x04},	// {
	{0x04, 0x02},	// |
	{0x48, 0x01},	// }
	{0x00, 0x00}    //DEL (or end of table l=0x60)
#endif
};

// Segment table 2 for VFD clock display on text part
//
//  aaaaaaa 
// f       b
// f       b
// f       b
//  ggggggg
// e       c
// e       c
// e       c
//  ddddddd  
//
// a    b    f    g    e/c  c/e  d
// 0x40 0x20 0x10 0x08 0x04 0x02 0x01
//
static u8 NumLib[10][2] =
{                   //  abfgced   abfgecd
	{0x77, 0x77},	//{01110111, 01110111}, 0
	{0x24, 0x22},	//{00100100, 00010010}, 1
	{0x6B, 0x6D},	//{01101011, 01101101}, 2
	{0x6D, 0x6B},	//{01101101, 01101011}, 3
	{0x3C, 0x3A},	//{00111100, 00111010}, 4
	{0x5D, 0x5B},	//{01011101, 01011011}, 5
	{0x5F, 0x5F},	//{01011111, 01011111}, 6
	{0x64, 0x62},	//{01100100, 01100010}, 7
	{0x7F, 0x7F},	//{01111111, 01111111}, 8
	{0x7D, 0x7B}	//{01111101, 01111011}  9
	            	// even,     odd digits
};

#if defined(SPARK7162)
// dvfd_bitmap: Segment table for dot VFD display
static char dvfd_bitmap[95][5] =
{ // format seems to be 5 rows/bytes by 8 columns/bits
	{0x00, 0x00, 0x00, 0x00, 0x00, }, //' ' 0x20 032
	{0x10, 0x42, 0x08, 0x01, 0x04, }, //'!' 0x21 033
	{0x68, 0xad, 0x00, 0x00, 0x00, }, //'"' 0x22 034
	{0x28, 0xf5, 0xd5, 0x57, 0x0a, }, //'#' 0x23 035
	{0x10, 0x5f, 0x1c, 0x7d, 0x04, }, //'$' 0x24 036
	{0x8c, 0x89, 0x88, 0xc8, 0x18, }, //'%' 0x25 037
	{0x88, 0x52, 0x44, 0x4d, 0x16, }, //'&' 0x26 038
	{0x10, 0x42, 0x00, 0x00, 0x00, }, //''' 0x27 039
	{0x40, 0x44, 0x08, 0x41, 0x10, }, //'(' 0x28 040
	{0x04, 0x41, 0x08, 0x11, 0x01, }, //')' 0x29 041
	{0x90, 0xea, 0x88, 0xab, 0x04, }, //'*' 0x2a 042
	{0x00, 0x42, 0x3e, 0x21, 0x00, }, //'+' 0x2b 043
	{0x00, 0x00, 0x00, 0x31, 0x02, }, //',' 0x2c 044
	{0x00, 0xf0, 0x01, 0x00, 0x00, }, //'-' 0x2d 045
	{0x00, 0x00, 0x00, 0x61, 0x00, }, //'.' 0x2e 046
	{0x00, 0x88, 0x88, 0x08, 0x00, }, //'/' 0x2f 047
	{0xb8, 0x18, 0x63, 0x8c, 0x0e, }, //'0' 0x30 048
	{0x10, 0x43, 0x08, 0x21, 0x1f, }, //'1' 0x31 049
	{0xb8, 0x08, 0x11, 0x11, 0x1f, }, //'2' 0x32 050
	{0x38, 0xc8, 0x20, 0x84, 0x0f, }, //'3' 0x33 051
	{0x20, 0xa6, 0xd2, 0x47, 0x1c, }, //'4' 0x34 052
	{0xbc, 0xf0, 0x20, 0x84, 0x0f, }, //'5' 0x35 053
	{0x30, 0x11, 0x5e, 0x8c, 0x0e, }, //'6' 0x36 054
	{0x7c, 0x88, 0x10, 0x22, 0x04, }, //'7' 0x37 055
	{0xb8, 0x18, 0x5d, 0x8c, 0x0e, }, //'8' 0x38 056
	{0xb8, 0x18, 0x3d, 0x44, 0x06, }, //'9' 0x39 057
	{0x00, 0x46, 0x00, 0x61, 0x00, }, //':' 0x3a 058
	{0x00, 0x46, 0x00, 0x31, 0x02, }, //';' 0x3b 059
	{0x40, 0x26, 0x06, 0x81, 0x00, }, //'<' 0x3c 060
	{0x80, 0x0f, 0x3e, 0x00, 0x00, }, //'=' 0x3d 061
	{0x04, 0x83, 0x30, 0x09, 0x00, }, //'>' 0x3e 062
	{0xb8, 0x08, 0x19, 0x01, 0x04, }, //'?' 0x3f 063
	{0xb8, 0x98, 0x6b, 0x0e, 0x1e, }, //'@' 0x40 064
	{0x08, 0x93, 0xe2, 0x8f, 0x11, }, //'A' 0x41 065
	{0xbc, 0x18, 0x5f, 0x8c, 0x0f, }, //'B' 0x42 066
	{0xb8, 0x18, 0x42, 0x88, 0x0e, }, //'C' 0x43 067
	{0xbc, 0x18, 0x63, 0x8c, 0x0f, }, //'D' 0x44 068
	{0xfc, 0x18, 0x4e, 0x89, 0x1f, }, //'E' 0x45 069
	{0x78, 0x21, 0x9c, 0x12, 0x06, }, //'F' 0x46 070
	{0xf8, 0x10, 0x42, 0x8e, 0x1e, }, //'G' 0x47 071
	{0xc4, 0x18, 0x7f, 0x8c, 0x11, }, //'H' 0x48 072
	{0x38, 0x42, 0x08, 0x21, 0x0e, }, //'I' 0x49 073
	{0x70, 0x08, 0x61, 0x8c, 0x0e, }, //'J' 0x4a 074
	{0xec, 0x54, 0x4e, 0x4a, 0x13, }, //'K' 0x4b 075
	{0x1c, 0x21, 0x84, 0x10, 0x1f, }, //'L' 0x4c 076
	{0xc4, 0xbd, 0x6b, 0x8d, 0x1b, }, //'M' 0x4d 077
	{0xe4, 0x39, 0x6b, 0xce, 0x13, }, //'N' 0x4e 078
	{0xb8, 0x18, 0x63, 0x8c, 0x0e, }, //'O' 0x4f 079
	{0xbc, 0x18, 0x5f, 0x08, 0x03, }, //'P' 0x50 080
	{0xb8, 0x18, 0x63, 0x4d, 0x16, }, //'Q' 0x51 081
	{0xbc, 0x18, 0x5f, 0x49, 0x13, }, //'R' 0x52 082
	{0xb8, 0x18, 0x1c, 0x8c, 0x0e, }, //'S' 0x53 083
	{0xfc, 0x4a, 0x08, 0x21, 0x0e, }, //'T' 0x54 084
	{0xec, 0x18, 0x63, 0x8c, 0x0e, }, //'U' 0x55 085
	{0xc4, 0x18, 0xa5, 0x52, 0x04, }, //'V' 0x56 086
	{0xc4, 0x58, 0x6b, 0x55, 0x0a, }, //'W' 0x57 087
	{0x44, 0xa9, 0x88, 0xca, 0x11, }, //'X' 0x58 088
	{0x44, 0xa9, 0x08, 0x21, 0x0e, }, //'Y' 0x59 089
	{0xfc, 0x88, 0x88, 0x88, 0x1f, }, //'Z' 0x5a 090
	{0x30, 0x42, 0x08, 0x21, 0x0c, }, //'[' 0x5b 091
	{0x80, 0x20, 0x08, 0x82, 0x00, }, //'\' 0x5c 092
	{0x18, 0x42, 0x08, 0x21, 0x06, }, //']' 0x5d 093
	{0x00, 0xa2, 0x22, 0x00, 0x00, }, //'^' 0x5e 094
	{0x00, 0x00, 0x00, 0xf8, 0x00, }, //'_' 0x5f 095
#if defined(LOWER_CASE)
	{0x00, 0x02, 0x00, 0x00, 0x00, }, //'`' 0x60 096
	{0x00, 0x07, 0x7d, 0xf4, 0x00, }, //'a' 0x61 097
	{0x08, 0xe1, 0xa4, 0x74, 0x00, }, //'b' 0x62 098
	{0x00, 0x17, 0x43, 0xf0, 0x00, }, //'c' 0x63 099
	{0x20, 0xe4, 0x52, 0x72, 0x00, }, //'d' 0x64 100
	{0x00, 0x17, 0x7f, 0xf0, 0x00, }, //'e' 0x65 101
	{0x60, 0xe2, 0x09, 0x21, 0x0e, }, //'f' 0x66 102
	{0x00, 0x26, 0x25, 0x87, 0x0e, }, //'g' 0x67 103
	{0x08, 0xe1, 0xa4, 0x94, 0x00, }, //'h' 0x68 104
	{0x10, 0x60, 0x08, 0x71, 0x00, }, //'i' 0x69 105
	{0x10, 0xe0, 0x10, 0x52, 0x0c, }, //'j' 0x6a 106
	{0x08, 0x6d, 0x8c, 0x92, 0x00, }, //'k' 0x6b 107
	{0x18, 0x42, 0x08, 0x61, 0x00, }, //'l' 0x6c 108
	{0x00, 0x57, 0x6b, 0xad, 0x00, }, //'m' 0x6d 109
	{0x00, 0x26, 0xa5, 0x94, 0x00, }, //'n' 0x6e 110
	{0x00, 0x26, 0xa5, 0x64, 0x00, }, //'o' 0x6f 111
	{0x00, 0x27, 0xa5, 0x13, 0x02, }, //'p' 0x70 112
	{0x00, 0x97, 0x92, 0x43, 0x08, }, //'q' 0x71 113
	{0x80, 0x26, 0x84, 0x10, 0x00, }, //'r' 0x72 114
	{0x00, 0x17, 0x0c, 0x3a, 0x00, }, //'s' 0x73 115
	{0x88, 0x27, 0x84, 0x62, 0x00, }, //'t' 0x74 116
	{0x80, 0x1c, 0x63, 0x74, 0x00, }, //'u' 0x75 117
	{0x80, 0x18, 0x95, 0x22, 0x00, }, //'v' 0x76 118
	{0x80, 0x58, 0xeb, 0x56, 0x00, }, //'w' 0x77 119
	{0x80, 0xa9, 0x88, 0x9a, 0x00, }, //'x' 0x78 120
	{0x80, 0x29, 0x15, 0x23, 0x04, }, //'y' 0x79 121
	{0x80, 0x8f, 0x88, 0xf8, 0x00, }, //'z' 0x7a 122
	{0x60, 0x42, 0x04, 0x21, 0x18, }, //'{' 0x7b 123
	{0x10, 0x42, 0x08, 0x21, 0x04, }, //'|' 0x7c 124
	{0x0c, 0x42, 0x10, 0x21, 0x03, }, //'}' 0x7d 125
	{0x00, 0x20, 0x2a, 0x02, 0x00, }, //'~' 0x7e 126
#else
	{0x00, 0x02, 0x00, 0x00, 0x00, }, //'`' 0x60 096
	{0x08, 0x93, 0xe2, 0x8f, 0x11, }, //'A' 0x61 095
	{0xbc, 0x18, 0x5f, 0x8c, 0x0f, }, //'B' 0x62 076
	{0xb8, 0x18, 0x42, 0x88, 0x0e, }, //'C' 0x63 087
	{0xbc, 0x18, 0x63, 0x8c, 0x0f, }, //'D' 0x64 099
	{0xfc, 0x18, 0x4e, 0x89, 0x1f, }, //'E' 0x65 100
	{0x78, 0x21, 0x9c, 0x12, 0x06, }, //'F' 0x66 101
	{0xf8, 0x10, 0x42, 0x8e, 0x1e, }, //'G' 0x67 102
	{0xc4, 0x18, 0x7f, 0x8c, 0x11, }, //'H' 0x68 103
	{0x38, 0x42, 0x08, 0x21, 0x0e, }, //'I' 0x69 104
	{0x70, 0x08, 0x61, 0x8c, 0x0e, }, //'J' 0x6a 105
	{0xec, 0x54, 0x4e, 0x4a, 0x13, }, //'K' 0x6b 106
	{0x1c, 0x21, 0x84, 0x10, 0x1f, }, //'L' 0x6c 107
	{0xc4, 0xbd, 0x6b, 0x8d, 0x1b, }, //'M' 0x6d 108
	{0xe4, 0x39, 0x6b, 0xce, 0x13, }, //'N' 0x6e 109
	{0xb8, 0x18, 0x63, 0x8c, 0x0e, }, //'O' 0x6f 110
	{0xbc, 0x18, 0x5f, 0x08, 0x03, }, //'P' 0x70 111
	{0xb8, 0x18, 0x63, 0x4d, 0x16, }, //'Q' 0x71 112
	{0xbc, 0x18, 0x5f, 0x49, 0x13, }, //'R' 0x72 114
	{0xb8, 0x18, 0x1c, 0x8c, 0x0e, }, //'S' 0x73 115
	{0xfc, 0x4a, 0x08, 0x21, 0x0e, }, //'T' 0x74 116
	{0xec, 0x18, 0x63, 0x8c, 0x0e, }, //'U' 0x75 117
	{0xc4, 0x18, 0xa5, 0x52, 0x04, }, //'V' 0x76 118
	{0xc4, 0x58, 0x6b, 0x55, 0x0a, }, //'W' 0x77 119
	{0x44, 0xa9, 0x88, 0xca, 0x11, }, //'X' 0x78 120
	{0x44, 0xa9, 0x08, 0x21, 0x0e, }, //'Y' 0x79 121
	{0xfc, 0x88, 0x88, 0x88, 0x1f, }, //'Z' 0x7a 122
	{0x60, 0x42, 0x04, 0x21, 0x18, }, //'{' 0x7b 123
	{0x10, 0x42, 0x08, 0x21, 0x04, }, //'|' 0x7c 124
	{0x0c, 0x42, 0x10, 0x21, 0x03, }, //'}' 0x7d 125
	{0x00, 0x20, 0x2a, 0x02, 0x00, }, //'~' 0x7e 126
#endif
};
#endif

#if defined(SPARK)
//lwj add begin
// Segment table for LED display (Spark)
//
//  aaaaaaa 
// f       b
// f       b
// f       b
//  ggggggg
// e       c
// e       c
// e       c
//  ddddddd   h
//
// a    b    c    d    e    f    g    h
// 0x80 0x40 0x20 0x10 0x08 0x04 0x02 0x01
//
// LED segments: 88.88 (segment h only available on position 2)

static u8 ywpanel_led_map[0x80] =
{	//0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*00-0f*/
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*10-1f*/
	0x00, 0x40, 0x44, 0x6a, 0xb6, 0x4a, 0x00, 0x40, 0x9c, 0xf0, 0x6e, 0x00, 0x30, 0x02, 0x01, 0x4a, /*20-2f*/
	//      !     "     #     $     %     &     '     (     )     *     +     ,     -     .     /
	0xfc, 0x60, 0xda, 0xf2, 0x66, 0xb6, 0xbe, 0xe0, 0xfe, 0xf6, 0x00, 0x00, 0x1a, 0x12, 0x32, 0xca, /*30-3f*/
	//0     1     2     3     4     5     6     7     8     9     :     ;     <     =     >     ?
	0xde, 0xee, 0x3e, 0x9c, 0x7a, 0x9e, 0x8e, 0xbc, 0x2e, 0x60, 0x70, 0x0e, 0x1c, 0xaa, 0x2a, 0x3a, /*40-4f*/
	//@     A     B     C     D     E     F     G     H     I     J     K     L     M     N     O
	0xce, 0xe6, 0x0a, 0xb6, 0x1e, 0x7c, 0x38, 0x56, 0x6e, 0x76, 0xda, 0x9c, 0x26, 0xf0, 0xc4, 0x10, /*50-5f*/
	//P     Q     R     S     T     U     V     W     X     Y     Z     [     \     ]     ^     _
	0x04, 0xfa, 0x3e, 0x1a, 0x7a, 0xde, 0x8e, 0xf6, 0x2e, 0x20, 0x70, 0x0e, 0x1c, 0xaa, 0x2a, 0x3a, /*60-6f*/
	//`     a     b     c     d     e     f     g     h     i     j     k     l     m     n     o
	0xce, 0xe6, 0x0a, 0xb6, 0x1e, 0x38, 0x38, 0x56, 0x6e, 0x76, 0xda, 0x9c, 0x0c, 0xf0, 0x02, 0x00  /*70-7f*/
	//p     q     r     s     t     u     v     w     x     y     z     {     |     }     ~    DEL
};
//lwj add end
#endif

enum
{
	YWPANEL_INIT_INSTR_GETVERSION = 0x50,

	YWPANEL_INIT_INSTR_GETCPUSTATE = 0x52,
	YWPANEL_INIT_INSTR_SETCPUSTATE,

	YWPANEL_INIT_INSTR_GETSTBYKEY1,
	YWPANEL_INIT_INSTR_GETSTBYKEY2,
	YWPANEL_INIT_INSTR_GETSTBYKEY3,
	YWPANEL_INIT_INSTR_GETSTBYKEY4,
	YWPANEL_INIT_INSTR_GETSTBYKEY5,

	YWPANEL_INIT_INSTR_SETSTBYKEY1,
	YWPANEL_INIT_INSTR_SETSTBYKEY2,
	YWPANEL_INIT_INSTR_SETSTBYKEY3,
	YWPANEL_INIT_INSTR_SETSTBYKEY4,
	YWPANEL_INIT_INSTR_SETSTBYKEY5,

	YWPANEL_INIT_INSTR_GETIRCODE,             /* 0x5e */
	YWPANEL_INIT_INSTR_SETIRCODE,

	YWPANEL_INIT_INSTR_GETENCRYPTMODE,        /* 0x60 */
	YWPANEL_INIT_INSTR_SETENCRYPTMODE,

	YWPANEL_INIT_INSTR_GETENCRYPTKEY,         /* 0x62 */
	YWPANEL_INIT_INSTR_SETENCRYPTKEY,

	YWPANEL_INIT_INSTR_GETVERIFYSTATE,        /* 0x64 */
	YWPANEL_INIT_INSTR_SETVERIFYSTATE,

	YWPANEL_INIT_INSTR_GETTIME = 0x66,        /* 0x66 */
	YWPANEL_INIT_INSTR_SETTIME,               /* 0x67 */
	YWPANEL_INIT_INSTR_CONTROLTIMER,          /* 0x68 */
	YWPANEL_INIT_INSTR_SETPOWERONTIME,        /* 0x69 */
	YWPANEL_INIT_INSTR_GETPOWERONTIME,        /* 0x6a */

	YWPANEL_INIT_INSTR_GETFPSTANDBYSTATE,     /* 0x6b */
	YWPANEL_INIT_INSTR_SETFPSTANDBYSTATE,     /* 0x6c */

	YWPANEL_INIT_INSTR_GETBLUEKEY1,           /* 0x6d */
	YWPANEL_INIT_INSTR_GETBLUEKEY2,
	YWPANEL_INIT_INSTR_GETBLUEKEY3,
	YWPANEL_INIT_INSTR_GETBLUEKEY4,           /* 0x70 */
	YWPANEL_INIT_INSTR_GETBLUEKEY5,

	YWPANEL_INIT_INSTR_SETBLUEKEY1,           /* 0x72 */
	YWPANEL_INIT_INSTR_SETBLUEKEY2,
	YWPANEL_INIT_INSTR_SETBLUEKEY3,
	YWPANEL_INIT_INSTR_SETBLUEKEY4,
	YWPANEL_INIT_INSTR_SETBLUEKEY5,           /* 0x76 */

	YWPANEL_INIT_INSTR_GETPOWERONSTATE,       /* 0x77 */
	YWPANEL_INIT_INSTR_SETPOWERONSTATE,       /* 0x78 */

	YWPANEL_INIT_INSTR_GETSTARTUPSTATE,       /* 0x79 */
	YWPANEL_INIT_INSTR_GETLOOPSTATE = 0x7a,   /* 0x7a */
	YWPANEL_INIT_INSTR_SETLOOPSTATE,          /* 0x7b */

	YWPANEL_INIT_INSTR_SETDVFDDISPLAY = 0x80, /* 0x80 */
	YWPANEL_INIT_INSTR_STRDVFDBRIGHTNESS,
	YWPANEL_INIT_INSTR_SETDVFDTIMEMODE,
	YWPANEL_INIT_INSTR_GETDVFDTIMEMODE
};

enum YWPANL_READ_INSTR_e
{
	YWPANEL_READ_INSTR_ACK     = 0x10,
	YWPANEL_READ_INSTR_SCANKEY = 0x11,
	YWPANEL_READ_INSTR_IRKEY   = 0x12,
	YWPANEL_READ_INSTR_VFDKEY  = 0x13
};

enum YWPANL_WRITE_INSTR_e
{
	YWPANEL_DISPLAY_INSTR_LBD = 0x30,
	YWPANEL_DISPLAY_INSTR_LED,
	YWPANEL_DISPLAY_INSTR_LCD,
	YWPANEL_DISPLAY_INSTR_VFD,
	YWPANEL_DISPLAY_INSTR_DVFD_SINGLE,
	YWPANEL_DISPLAY_INSTR_DVFD
};

#define FP_CS_CLR() {udelay(10);stpio_set_pin(pio_cs, 0);}
#define FP_CS_SET() {udelay(10);stpio_set_pin(pio_cs, 1);}

static SegAddrVal_T VfdSegAddr[15];
static struct i2c_adapter* panel_i2c_adapter;
// const u16 cnCRC_16 = 0x8005;
// CRC-16 = X16 + X15 + X2 + X0
const u16 cnCRC_CCITT = 0x1021;
// CRC-CCITT = X16 + X12 + X5 + X0

u32 Table_CRC[256]; // CRC

void YWPANEL_BuildTable16(u16 aPoly)
{
	u16 i, j;
	u16 nData;
	u16 nAccum;
	for (i = 0; i < 256; i++)
	{
		nData = (u16)(i << 8);
		nAccum = 0;
		for (j = 0; j < 8; j++)
		{
			if ((nData ^ nAccum) & 0x8000)
			{
				nAccum = (nAccum << 1) ^ aPoly;
			}
			else
			{
				nAccum <<= 1;
			}
			nData <<= 1;
		}
		Table_CRC[i] = (u32)nAccum;
	 }
}

u16 YWPANEL_GenerateCRC16(u8 * buffer, u32 bufLength)
{
	u32 i;
	u16 nAccum = 0;
	YWPANEL_BuildTable16(cnCRC_CCITT);
	for (i = 0; i < bufLength; i++)
	{
		nAccum = (nAccum << 8) ^ (u16)Table_CRC[(nAccum >> 8) ^ *buffer++];
	}
	return nAccum;
}

#if defined(SPARK7162)
// Code for dot VFD
bool YWPANEL_FP_DvfdFillCmd(YWPANEL_FPData_t *data, YWPANEL_I2CData_t *I2CData)
{
	switch (data->data.dvfdData.type)
	{
		case YWPANEL_DVFD_DISPLAYSTRING:
		{
			I2CData->writeBuff[0] = YWPANEL_DISPLAY_INSTR_DVFD;
			break;
		}
		case YWPANEL_DVFD_DISPLAYSYNC:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETDVFDDISPLAY;
			break;
		}
		case YWPANEL_DVFD_SETTIMEMODE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETDVFDTIMEMODE;
			break;
		}
		case YWPANEL_DVFD_GETTIMEMODE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETDVFDTIMEMODE;
//			break;
		}
		default:
		{
			break;
		}
	}
	return true;
}

bool YWPANEL_FP_DvfdFillLen(YWPANEL_FPData_t *data, YWPANEL_I2CData_t *I2CData)
{
	if (data->data.dvfdData.type == YWPANEL_DVFD_DISPLAYSTRING)
	{
		I2CData->writeBuff[1] = 1 + (5 + 1) * 4;
	}
	else
	{
		I2CData->writeBuff[1] = 0x04;
	}
	return true;
}

void YWPANEL_FP_DvfdFillString(YWPANEL_FPData_t *data, YWPANEL_I2CData_t *I2CData, u8 uMax)
{
	int i;

	for (i = 0; i < uMax; i++)
	{
		I2CData->writeBuff[6 * i + 3] = data->data.dvfdData.address[i];
		I2CData->writeBuff[6 * i + 4] = data->data.dvfdData.DisplayValue[i][0];
		I2CData->writeBuff[6 * i + 5] = data->data.dvfdData.DisplayValue[i][1];
		I2CData->writeBuff[6 * i + 6] = data->data.dvfdData.DisplayValue[i][2];
		I2CData->writeBuff[6 * i + 7] = data->data.dvfdData.DisplayValue[i][3];
		I2CData->writeBuff[6 * i + 8] = data->data.dvfdData.DisplayValue[i][4];
	}
}

bool YWPANEL_FP_DvfdFillData(YWPANEL_FPData_t *data, YWPANEL_I2CData_t *I2CData)
{
	switch (data->data.dvfdData.type)
	{
		case YWPANEL_DVFD_DISPLAYSTRING:
		{
			u8 uMax = data->data.dvfdData.ulen;
			if (uMax > 4)
			{
			    uMax = 4;
			}
			I2CData->writeBuff[2] = uMax;
			YWPANEL_FP_DvfdFillString(data, I2CData, uMax);
			break;
		}
		case YWPANEL_DVFD_SETTIMEMODE:
		{
			I2CData->writeBuff[2] = data->data.dvfdData.setValue;
//			break;
		}
		default:
		{
			break;
		}
	}
	return true;
}

bool YWPANEL_FP_DvfdFillCrc(YWPANEL_FPData_t *data, YWPANEL_I2CData_t *I2CData)
{
	u16 usCRC16 = 0;

	if (data->data.dvfdData.type == YWPANEL_DVFD_DISPLAYSTRING)
	{
		usCRC16 = YWPANEL_GenerateCRC16(I2CData->writeBuff, 27);
		I2CData->writeBuff[27] = (usCRC16 & 0xff);
		I2CData->writeBuff[28] = ((usCRC16 >> 8) & 0xff);
		I2CData->writeBuffLen = 29;
	}
	else
	{
		usCRC16 = YWPANEL_GenerateCRC16(I2CData->writeBuff, 6);
		I2CData->writeBuff[6] = (usCRC16 & 0xff);
		I2CData->writeBuff[7] = ((usCRC16 >> 8) & 0xff);
		I2CData->writeBuffLen = 8;
	}
	return true;
}
#endif //defined(SPARK7162)

int YWPANEL_FP_SetI2cData(YWPANEL_FPData_t *data,YWPANEL_I2CData_t *I2CData)
{
	u16 usCRC16 = 0;

	if ((data == NULL)
	|| (I2CData == NULL)
	|| (data->dataType < YWPANEL_DATATYPE_LBD)
	|| (data->dataType > YWPANEL_DATATYPE_NUM))
	{
		PANEL_DEBUG("Bad parameter\n");
		return false;
	}

	switch (data->dataType)
	{
		case YWPANEL_DATATYPE_LBD:
		{
			I2CData->writeBuff[0] = YWPANEL_DISPLAY_INSTR_LBD;
			break;
		}
#if defined(SPARK)
		case YWPANEL_DATATYPE_LED:
		{
			I2CData->writeBuff[0] = YWPANEL_DISPLAY_INSTR_LED;  //d48zm modify
			break;
		}
#elif 1
		case YWPANEL_DATATYPE_VFD:
		{
			I2CData->writeBuff[0] = YWPANEL_DISPLAY_INSTR_VFD;
			break;
		}
#elif defined(SPARK7162)
		case YWPANEL_DATATYPE_DVFD:
		{
			YWPANEL_FP_DvfdFillCmd(data, I2CData);
			break;
		}
#endif
		case YWPANEL_DATATYPE_SCANKEY:
		{
			I2CData->writeBuff[0] = YWPANEL_READ_INSTR_SCANKEY;
			break;
		}
		case YWPANEL_DATATYPE_GETVERSION:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETVERSION;
			break;
		}
		case YWPANEL_DATATYPE_GETCPUSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETCPUSTATE;
			break;
		}
		case YWPANEL_DATATYPE_SETCPUSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETCPUSTATE;
			break;
		}
		case YWPANEL_DATATYPE_GETSTARTUPSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETSTARTUPSTATE;
			break;
		}
		case YWPANEL_DATATYPE_GETFPSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETFPSTANDBYSTATE;
			break;
		}
		case YWPANEL_DATATYPE_SETFPSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETFPSTANDBYSTATE;
			break;
		}
		case YWPANEL_DATATYPE_GETPOWERONSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETPOWERONSTATE;
			break;
		}
		case YWPANEL_DATATYPE_SETPOWERONSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETPOWERONSTATE;
			break;
		}
		case YWPANEL_DATATYPE_GETSTBYKEY1:
		case YWPANEL_DATATYPE_GETSTBYKEY2:
		case YWPANEL_DATATYPE_GETSTBYKEY3:
		case YWPANEL_DATATYPE_GETSTBYKEY4:
		case YWPANEL_DATATYPE_GETSTBYKEY5:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETSTBYKEY1 + (data->dataType - YWPANEL_DATATYPE_GETSTBYKEY1);
			break;
		}
		case YWPANEL_DATATYPE_SETSTBYKEY1:
		case YWPANEL_DATATYPE_SETSTBYKEY2:
		case YWPANEL_DATATYPE_SETSTBYKEY3:
		case YWPANEL_DATATYPE_SETSTBYKEY4:
		case YWPANEL_DATATYPE_SETSTBYKEY5:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETSTBYKEY1 + (data->dataType - YWPANEL_DATATYPE_SETSTBYKEY1);
			break;
		}
		case YWPANEL_DATATYPE_GETBLUEKEY1:
		case YWPANEL_DATATYPE_GETBLUEKEY2:
		case YWPANEL_DATATYPE_GETBLUEKEY3:
		case YWPANEL_DATATYPE_GETBLUEKEY4:
		case YWPANEL_DATATYPE_GETBLUEKEY5:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETBLUEKEY1 + (data->dataType - YWPANEL_DATATYPE_GETBLUEKEY1);
			break;
		}
		case YWPANEL_DATATYPE_SETBLUEKEY1:
		case YWPANEL_DATATYPE_SETBLUEKEY2:
		case YWPANEL_DATATYPE_SETBLUEKEY3:
		case YWPANEL_DATATYPE_SETBLUEKEY4:
		case YWPANEL_DATATYPE_SETBLUEKEY5:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETBLUEKEY1 + (data->dataType - YWPANEL_DATATYPE_SETBLUEKEY1);
			break;
		}
		case YWPANEL_DATATYPE_GETVERIFYSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETVERIFYSTATE;
			break;
		}
		case YWPANEL_DATATYPE_SETVERIFYSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETVERIFYSTATE;
			break;
		}
		case YWPANEL_DATATYPE_GETTIME:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETTIME;
			break;
		}
		case YWPANEL_DATATYPE_SETTIME:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETTIME;
			break;
		}
		case YWPANEL_DATATYPE_CONTROLTIMER:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_CONTROLTIMER;
			break;
		}
		case YWPANEL_DATATYPE_SETPOWERONTIME:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETPOWERONTIME;
			break;
		}
		case YWPANEL_DATATYPE_GETPOWERONTIME:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETPOWERONTIME;
			break;
		}
		case YWPANEL_DATATYPE_SETLOOPSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_SETLOOPSTATE;
			break;
		}
		case YWPANEL_DATATYPE_GETLOOPSTATE:
		{
			I2CData->writeBuff[0] = YWPANEL_INIT_INSTR_GETLOOPSTATE;
		}
		default:
		{
			break;
		}
	}

	switch (data->dataType)
	{
#if defined(SPARK7162)
		case YWPANEL_DATATYPE_DVFD:
		{
			YWPANEL_FP_DvfdFillLen(data, I2CData);
			break;
		}
#endif
		case YWPANEL_DATATYPE_VFD:
		{
			if (data->data.vfdData.type == YWPANEL_VFD_DISPLAYSTRING)
			{
				I2CData->writeBuff[1] = 0x22;
				break;
			}
		}
		default:
		{
			I2CData->writeBuff[1] = 0x04;
			break;
		}
	}

	switch (data->dataType)
	{
		case YWPANEL_DATATYPE_LBD:
		{
			I2CData->writeBuff[2] = data->data.lbdData.value;
			break;
		}
#if defined(SPARK)
		case YWPANEL_DATATYPE_LED:
		{
			I2CData->writeBuff[2] = data->data.ledData.led1;
			I2CData->writeBuff[3] = data->data.ledData.led2;
			I2CData->writeBuff[4] = data->data.ledData.led3;
			I2CData->writeBuff[5] = data->data.ledData.led4;
			break;
		}
#endif
		case YWPANEL_DATATYPE_VFD:
		{
			I2CData->writeBuff[2] = data->data.vfdData.type;
			switch (data->data.vfdData.type)
			{
				case YWPANEL_VFD_SETTING:
				{
					I2CData->writeBuff[3] = data->data.vfdData.setValue;
					break;
				}
				case YWPANEL_VFD_DISPLAY:
				{
					I2CData->writeBuff[3] = data->data.vfdData.address[0];
					I2CData->writeBuff[4] = data->data.vfdData.DisplayValue[0];
					break;
				}
				case YWPANEL_VFD_READKEY:
				{
					I2CData->writeBuff[3] = data->data.vfdData.key;
					break;
				}
				case YWPANEL_VFD_DISPLAYSTRING:
				{
					int i;
					for (i = 0; i < 16; i++)
					{
						I2CData->writeBuff[4 + 2 * i] = data->data.vfdData.address[i];
						I2CData->writeBuff[4 + 2 * i + 1] = data->data.vfdData.DisplayValue[i];
					}
					break;
				}
			}
			break;
		}
#if defined(SPARK7162)
		case YWPANEL_DATATYPE_DVFD:
		{
			YWPANEL_FP_DvfdFillData(data, I2CData);
			break;
		}
#endif
		case YWPANEL_DATATYPE_SETCPUSTATE:
		{
			I2CData->writeBuff[2] = data->data.CpuState.state;
			break;
		}
		case YWPANEL_DATATYPE_GETSTARTUPSTATE:
		{
			I2CData->writeBuff[2] = data->data.StartUpState.state;
			break;
		}
		case YWPANEL_DATATYPE_SETFPSTATE:
		{
			I2CData->writeBuff[2] = data->data.FpStandbyState.On;
			break;
		}
		case YWPANEL_DATATYPE_SETPOWERONSTATE:
		{
			I2CData->writeBuff[2] = data->data.PowerOnState.state;
			break;
		}
		case YWPANEL_DATATYPE_SETLOOPSTATE:
		{
			I2CData->writeBuff[2] = data->data.LoopState.state;
			break;
		}
		case YWPANEL_DATATYPE_SETVERIFYSTATE:
		{
			I2CData->writeBuff[2] = data->data.verifyState.state;
			break;
		}
		case YWPANEL_DATATYPE_SETTIME:
		case YWPANEL_DATATYPE_SETPOWERONTIME:
		{
			I2CData->writeBuff[2] = (u8)((data->data.time.second >> 24) & 0xff);
			I2CData->writeBuff[3] = (u8)((data->data.time.second >> 16) & 0xff);
			I2CData->writeBuff[4] = (u8)((data->data.time.second >>  8) & 0xff);
			I2CData->writeBuff[5] = (u8)(data->data.time.second         & 0xff);
			break;
		}
		case YWPANEL_DATATYPE_CONTROLTIMER:
		{
			I2CData->writeBuff[2] = data->data.TimeState.startFlag;
			break;
		}
		case YWPANEL_DATATYPE_SETSTBYKEY1:
		case YWPANEL_DATATYPE_SETSTBYKEY2:
		case YWPANEL_DATATYPE_SETSTBYKEY3:
		case YWPANEL_DATATYPE_SETSTBYKEY4:
		case YWPANEL_DATATYPE_SETSTBYKEY5:
		case YWPANEL_DATATYPE_SETBLUEKEY1:
		case YWPANEL_DATATYPE_SETBLUEKEY2:
		case YWPANEL_DATATYPE_SETBLUEKEY3:
		case YWPANEL_DATATYPE_SETBLUEKEY4:
		case YWPANEL_DATATYPE_SETBLUEKEY5:
		{
			I2CData->writeBuff[2] = (u8)((data->data.stbyKey.key >> 24) & 0xff);
			I2CData->writeBuff[3] = (u8)((data->data.stbyKey.key >> 16) & 0xff);
			I2CData->writeBuff[4] = (u8)((data->data.stbyKey.key >>  8) & 0xff);
			I2CData->writeBuff[5] = (u8)(data->data.stbyKey.key         & 0xff);
			break;
		}
		default:
		{
			break;
		}
	}

	switch (data->dataType)
	{
#if defined(SPARK7162)
		case YWPANEL_DATATYPE_DVFD:
		{
			YWPANEL_FP_DvfdFillCrc(data, I2CData);
			break;
		}
#endif
		case YWPANEL_DATATYPE_VFD:
		{
			if (data->data.vfdData.type == YWPANEL_VFD_DISPLAYSTRING)
			{
				usCRC16 = YWPANEL_GenerateCRC16(I2CData->writeBuff, 36);
				I2CData->writeBuff[36] =  (usCRC16 &0xff);
				I2CData->writeBuff[37] =  ((usCRC16>>8) &0xff);
				I2CData->writeBuffLen = 38;
				break;
			}
		}
		default:
		{
			usCRC16 = YWPANEL_GenerateCRC16(I2CData->writeBuff, 6);
			I2CData->writeBuff[6] = (usCRC16 & 0xff);
			I2CData->writeBuff[7] = ((usCRC16 >> 8) & 0xff);
			I2CData->writeBuffLen = 8;
			break;
		}
	}
	return true;
}

int YWPANEL_FP_ParseI2cData(YWPANEL_FPData_t *data,YWPANEL_I2CData_t *I2CData)
{
	u16 crc16Code = 0;
	u16 receiveCode = 0;
	u8 dataType;
	u8 datalength;

	if ((data == NULL) || (I2CData == NULL))
	{
		ywtrace_print(TRACE_ERROR,"%s::error at %d\n",__func__,__LINE__);
		return false;
	}
	receiveCode =((u16)(I2CData->readBuff[7] << 8) & 0xff00) | ((u16)(I2CData->readBuff[6]) & 0xff);
	crc16Code = YWPANEL_GenerateCRC16(I2CData->readBuff, 6);

	if (receiveCode != crc16Code)
	{
		ywtrace_print(TRACE_ERROR,"CRC16 check is wrong at line %d. \n",__LINE__);
		// return false;  //YWDRIVER_MODI lwj remove
	}

	//ywtrace_print(TRACE_INFO,"%s::date->dateType=[0x%x]\n",__func__,data->dataType);
	dataType = I2CData->readBuff[0];
	datalength = I2CData->readBuff[1];

	//zy 2008-10-07
	switch (data->dataType)
	{
		case YWPANEL_DATATYPE_LBD:
		case YWPANEL_DATATYPE_LCD:
		case YWPANEL_DATATYPE_LED:
		case YWPANEL_DATATYPE_SETCPUSTATE:
		case YWPANEL_DATATYPE_SETVERIFYSTATE:
		case YWPANEL_DATATYPE_SETTIME:
		case YWPANEL_DATATYPE_CONTROLTIMER:
		case YWPANEL_DATATYPE_SETPOWERONTIME:
		case YWPANEL_DATATYPE_SETFPSTANDBYSTATE:
		case YWPANEL_DATATYPE_SETPOWERONSTATE:
		case YWPANEL_DATATYPE_SETLOOPSTATE:
		case YWPANEL_DATATYPE_SETSTBYKEY1:
		case YWPANEL_DATATYPE_SETSTBYKEY2:
		case YWPANEL_DATATYPE_SETSTBYKEY3:
		case YWPANEL_DATATYPE_SETSTBYKEY4:
		case YWPANEL_DATATYPE_SETSTBYKEY5:
		case YWPANEL_DATATYPE_SETBLUEKEY1:
		case YWPANEL_DATATYPE_SETBLUEKEY2:
		case YWPANEL_DATATYPE_SETBLUEKEY3:
		case YWPANEL_DATATYPE_SETBLUEKEY4:
		case YWPANEL_DATATYPE_SETBLUEKEY5:
		{
			if (dataType != YWPANEL_READ_INSTR_ACK)
			{
				ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
				return false;
			}
			break;
		}
		case YWPANEL_DATATYPE_SCANKEY:
		{
			if (dataType != YWPANEL_READ_INSTR_SCANKEY)
			{
				ywtrace_print(TRACE_ERROR,"%s: Error at %d, dataType = %d\n",__func__,__LINE__, dataType);
				return false;
			}
			break;
		}
		case YWPANEL_DATATYPE_VFD:
		{
			if (data->data.vfdData.type == YWPANEL_VFD_READKEY)
			{
				if (dataType != YWPANEL_READ_INSTR_VFDKEY)
				{
					ywtrace_print(TRACE_ERROR,"%s: Error at %d, dataType = %d\n",__func__,__LINE__, dataType);
					return false;
				}
			}
			else
			{
				if (dataType != YWPANEL_READ_INSTR_ACK)
				{
					ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
					return false;
				}
			}
			break;
		}
#if defined(SPARK7162)
		case YWPANEL_DATATYPE_DVFD:
		{
			if (data->data.dvfdData.type == YWPANEL_DVFD_DISPLAYSTRING)
			{
				if (dataType != YWPANEL_READ_INSTR_ACK)
				{
					ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
					return false;
				}
			}
			else if (data->data.dvfdData.type == YWPANEL_DVFD_SETTIMEMODE)
			{
				if (dataType != YWPANEL_READ_INSTR_ACK)
				{
					ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
					return false;
				}
			}
			else if (data->data.dvfdData.type == YWPANEL_DVFD_GETTIMEMODE)
			{
				if (dataType != YWPANEL_INIT_INSTR_GETDVFDTIMEMODE)
				{
					ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
					return false;
				}
			}
			else
			{
				if (dataType != YWPANEL_READ_INSTR_ACK)
				{
					ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
					return false;
				}
			}
			break;
		}
#endif // defined(SPARK7162)
		case YWPANEL_DATATYPE_GETPOWERONTIME:
		{
			if (dataType != YWPANEL_INIT_INSTR_GETPOWERONTIME)
			{
				return false;
			}
			break;
		}
		case YWPANEL_DATATYPE_GETPOWERONSTATE:
		{
			if (dataType != YWPANEL_INIT_INSTR_GETPOWERONSTATE)
			{
				return false;
			}
			break;
		}
		case YWPANEL_DATATYPE_GETSTARTUPSTATE:
		{
			if (dataType != YWPANEL_INIT_INSTR_GETSTARTUPSTATE)
			{
				return false;
			}
			break;
		}
		case YWPANEL_DATATYPE_GETLOOPSTATE:
		{
			if (dataType != YWPANEL_INIT_INSTR_GETLOOPSTATE)
			{
				return false;
			}
			break;
		}
		case YWPANEL_DATATYPE_GETTIME:
		{
			if (dataType != YWPANEL_INIT_INSTR_GETTIME)
			{
				ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
				return false;
			}
			break;
		}
		case YWPANEL_DATATYPE_GETSTBYKEY1:
		case YWPANEL_DATATYPE_GETSTBYKEY2:
		case YWPANEL_DATATYPE_GETSTBYKEY3:
		case YWPANEL_DATATYPE_GETSTBYKEY4:
		case YWPANEL_DATATYPE_GETSTBYKEY5:
		{
			if (dataType != (YWPANEL_INIT_INSTR_GETSTBYKEY1 + data->dataType -YWPANEL_DATATYPE_GETSTBYKEY1))
			{
				ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
				return false;
			}
			break;
		}
		case YWPANEL_DATATYPE_GETBLUEKEY1:
		case YWPANEL_DATATYPE_GETBLUEKEY2:
		case YWPANEL_DATATYPE_GETBLUEKEY3:
		case YWPANEL_DATATYPE_GETBLUEKEY4:
		case YWPANEL_DATATYPE_GETBLUEKEY5:
		{
			if (dataType != (YWPANEL_INIT_INSTR_GETBLUEKEY1 + data->dataType -YWPANEL_DATATYPE_GETBLUEKEY1))
			{
				ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
				return false;
			}
			break;
		}
		case YWPANEL_DATATYPE_GETVERSION:
		case YWPANEL_DATATYPE_GETCPUSTATE:
		case YWPANEL_DATATYPE_GETVERIFYSTATE:
		default:
		{
			break;
		}
	}

	switch (dataType)
	{
		case YWPANEL_READ_INSTR_ACK:  //ACK
		{
			data->ack = true;
			break;
		}
		case YWPANEL_READ_INSTR_SCANKEY:  //scan key
		{
			data->data.ScanKeyData.keyValue= I2CData->readBuff[2];
			data->ack = true;
			break;
		}
		case YWPANEL_READ_INSTR_VFDKEY:
		{
			data->data.vfdData.key = I2CData->readBuff[2];
			data->ack = true;
			break;
		}
		case YWPANEL_INIT_INSTR_GETVERSION: /* get version */
		{
			if (data->dataType != YWPANEL_DATATYPE_GETVERSION)
			{
				ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
				return false;
			}
			data->data.version.CpuType        = I2CData->readBuff[2];
			data->data.version.DisplayInfo    = (I2CData->readBuff[3] >> 4) & 0x0f;
			data->data.version.scankeyNum     = I2CData->readBuff[3] & 0x0f;
			data->data.version.swMajorVersion = I2CData->readBuff[4];
			data->data.version.swSubVersion   = I2CData->readBuff[5];

			data->ack = true;
			break;
		}
		case YWPANEL_INIT_INSTR_GETCPUSTATE: /* get CPU state */
		{
			if (data->dataType == YWPANEL_DATATYPE_GETCPUSTATE)
			{
				data->data.CpuState.state= I2CData->readBuff[2];
				data->ack = true;
			}
			else
			{
				ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
				return false;
			}
			break;
		}
		case YWPANEL_INIT_INSTR_GETFPSTANDBYSTATE: /* get VFD state */
		{
			data->data.FpStandbyState.On = I2CData->readBuff[2];
			data->ack = true;
			break;
		}
		case YWPANEL_INIT_INSTR_GETPOWERONSTATE: /* get power on state */
		{
			if (data->dataType == YWPANEL_DATATYPE_GETPOWERONSTATE)
			{
				data->data.PowerOnState.state= I2CData->readBuff[2];
				data->ack = true;
			}
			else
			{
				ywtrace_print(TRACE_ERROR,"%s: Error at %d\n",__func__,__LINE__);
				return false;
			}
			break;
		}
		case YWPANEL_INIT_INSTR_GETSTBYKEY1: /* get standby key */
		case YWPANEL_INIT_INSTR_GETSTBYKEY2:
		case YWPANEL_INIT_INSTR_GETSTBYKEY3:
		case YWPANEL_INIT_INSTR_GETSTBYKEY4:
		case YWPANEL_INIT_INSTR_GETSTBYKEY5:
		case YWPANEL_INIT_INSTR_GETBLUEKEY1: /* get blue key */
		case YWPANEL_INIT_INSTR_GETBLUEKEY2:
		case YWPANEL_INIT_INSTR_GETBLUEKEY3:
		case YWPANEL_INIT_INSTR_GETBLUEKEY4:
		case YWPANEL_INIT_INSTR_GETBLUEKEY5:
		{
			data->data.stbyKey.key = ((I2CData->readBuff[2] <<24) & 0xff000000)
			                       | ((I2CData->readBuff[3] <<16) & 0x00ff0000)
			                       | ((I2CData->readBuff[4] << 8) & 0x0000ff00)
			                       | ((I2CData->readBuff[5])      & 0x000000ff);
			data->ack = true;
			break;
		}
		case YWPANEL_INIT_INSTR_GETSTARTUPSTATE: /* get start up state */
		{
			data->data.StartUpState.state = I2CData->readBuff[2];
			data->ack = true;
			break;
		}
		case YWPANEL_INIT_INSTR_GETLOOPSTATE: /* get loopstate */
		{
			data->data.LoopState.state = I2CData->readBuff[2];
			data->ack = true;
			break;
		}
		case YWPANEL_INIT_INSTR_GETVERIFYSTATE: /* get verify state */
		{
			data->data.verifyState.state = I2CData->readBuff[2];
			break;
		}
		case YWPANEL_INIT_INSTR_GETPOWERONTIME:
		case YWPANEL_INIT_INSTR_GETTIME:
		{
			data->data.time.second = ((I2CData->readBuff[2] <<24) & 0xff000000)
			                       | ((I2CData->readBuff[3] <<16) & 0x00ff0000)
			                       | ((I2CData->readBuff[4] << 8) & 0x0000ff00)
			                       | ((I2CData->readBuff[5])      & 0x000000ff);
			data->ack = true;
			break;
		}
		default:
		{
			ywtrace_print(TRACE_ERROR,"%s: Error at line %d\n",__func__,__LINE__);
			return false;
		}
	}
	return true;
}

#ifdef CONFIG_CPU_SUBTYPE_STX7105
static int YWPANEL_FPWriteDataToI2c(struct i2c_adapter* I2CHandle, u8 * writeBuffer, u32 writeBufLen, u8 *readBuffer, u32 readBufLen)
{
	if (!isofti2c_write(writeBuffer, writeBufLen))
	{
		return false;
	}
	msleep(1);
	if (!isofti2c_read(readBuffer, readBufLen))
	{
		return false;
	}
	return true;
}
#else
static int YWPANEL_FPWriteDataToI2c(struct i2c_adapter* I2CHandle, u8 * writeBuffer, u32 writeBufLen, u8 *readBuffer, u32 readBufLen)
{
	int ret = 0;
	struct i2c_msg i2c_msg[] = {{ .addr = I2C_BUS_ADD, .flags = 0, .buf = writeBuffer, .len = writeBufLen},
	                            { .addr = I2C_BUS_ADD, .flags = I2C_M_RD, .buf = readBuffer, .len = readBufLen}};

	if (NULL == panel_i2c_adapter)
	{
		PANEL_DEBUG("Panel I2C failed\n");
		return -ENODEV;
	}
	//printk("%s:%d\n", __func__, __LINE__);
	ret = i2c_transfer(panel_i2c_adapter, &i2c_msg[0], 1);

	if (ret != 1)
	{
		ywtrace_print(TRACE_ERROR,"I2C read error at %d\n", __LINE__);
		return false;
	}

	msleep(1);
	//printk("%s:%d\n", __func__, __LINE__);
	ret = i2c_transfer(panel_i2c_adapter, &i2c_msg[1], 1);

	if (ret != 1)
	{
		ywtrace_print(TRACE_ERROR,"I2C read error at %d\n", __LINE__);
		return false;
	}
	return true;
}
#endif  /* 0 */

int YWPANEL_FP_SendData(YWPANEL_FPData_t *data)
{
	int ret = false;
	YWPANEL_I2CData_t I2CData;

	if (down_interruptible(&vfd_sem_rw))
	{
		ywtrace_print(TRACE_ERROR,"SendData is busy, you will be next!\n");
		return false;
	}

	if (data == NULL)
	{
		ywtrace_print(TRACE_ERROR,"Bad parameter at line %d\n",__LINE__);
		up(&vfd_sem_rw);
		return false;
	}

	memset(&I2CData, 0, sizeof(I2CData));

	if (YWPANEL_FP_SetI2cData(data,&I2CData) != true)
	{
		ywtrace_print(TRACE_ERROR,"SetI2cData error at line %d\n",__LINE__);
		up(&vfd_sem_rw);
		return false;
	}

	ret = YWPANEL_FPWriteDataToI2c(panel_i2c_adapter, I2CData.writeBuff, I2CData.writeBuffLen, I2CData.readBuff, YWPANEL_FP_INFO_MAX_LENGTH);

	if (ret != true)
	{
		ywtrace_print(TRACE_ERROR,"%s at %d\n",__func__,__LINE__);
		up(&vfd_sem_rw);
		return false;
	}

	ret = YWPANEL_FP_ParseI2cData(data, &I2CData);

	if (ret != true)
	{
		ywtrace_print(TRACE_ERROR,"%s at %d\n",__func__,__LINE__);
		up(&vfd_sem_rw);
		return false;
	}

	up(&vfd_sem_rw);
	return ret;
}

YWPANEL_FPSTATE_t YWPANEL_FP_GetFPStatus(void)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_GETFPSTATE;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d] \n",__LINE__);
		return false;
	}
	if ((data.data.FpStandbyState.On < YWPANEL_FPSTATE_STANDBYOFF) || (data.data.FpStandbyState.On > YWPANEL_FPSTATE_STANDBYON) )
	{
		return YWPANEL_FPSTATE_UNKNOWN;
	}
	return data.data.FpStandbyState.On;
}

int YWPANEL_FP_SetFPStatus(YWPANEL_FPSTATE_t On)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_SETFPSTATE;
	data.data.FpStandbyState.On = On;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return false;
	}
	return true;
}

YWPANEL_POWERONSTATE_t YWPANEL_FP_GetPowerOnStatus(void)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_GETPOWERONSTATE;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return false;
	}
	if ((data.data.PowerOnState.state < YWPANEL_POWERONSTATE_RUNNING) ||(data.data.PowerOnState.state > YWPANEL_POWERONSTATE_CHECKPOWERBIT) )
	{
		return YWPANEL_POWERONSTATE_UNKNOWN;
	}
	return data.data.PowerOnState.state;
}

int YWPANEL_FP_SetLoopState(YWPANEL_LOOPSTATE_t state)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_SETLOOPSTATE;
	data.data.LoopState.state = state;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return false;
	}
	return true;
}

int YWPANEL_FP_GetLoopState(YWPANEL_LOOPSTATE_t *state)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_GETLOOPSTATE;

	if(YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return false;
	}
	if((data.data.LoopState.state < YWPANEL_LOOPSTATE_UNKNOWN) || (data.data.LoopState.state > YWPANEL_LOOPSTATE_LOOPON) )
	{
		return false;
	}

	*state = data.data.LoopState.state;
	return true;
}

int YWPANEL_FP_SetPowerOnStatus(YWPANEL_POWERONSTATE_t state)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_SETPOWERONSTATE;
	data.data.PowerOnState.state = state;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return false;
	}
	return true;
}

int YWPANEL_FP_GetStartUpState(YWPANEL_STARTUPSTATE_t *state)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_GETSTARTUPSTATE;

	if (YWPANEL_FP_SendData(&data) != true)
	{
	  ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
	  return false;
	}

	*state = data.data.StartUpState.state;
	return true;
}

YWPANEL_CPUSTATE_t YWPANEL_FP_GetCpuStatus(void)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_GETCPUSTATE;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return false;
	}
	if ((data.data.CpuState.state <YWPANEL_CPUSTATE_RUNNING) ||(data.data.CpuState.state> YWPANEL_CPUSTATE_STANDBY) )
	{
		return YWPANEL_CPUSTATE_UNKNOWN;
	}

	return data.data.CpuState.state;
}

int YWPANEL_FP_SetCpuStatus(YWPANEL_CPUSTATE_t state)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_SETCPUSTATE;
	data.data.CpuState.state = state;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return false;
	}
	return true;
}

int YWPANEL_FP_GetVersion(YWPANEL_Version_t *version)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_GETVERSION;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return false;
	}

	memcpy(version, &(data.data.version), sizeof(YWPANEL_Version_t));
	return true;
}

int YWPANEL_FP_SetTime(u32 value)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_SETTIME;
	data.data.time.second = value;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return false;
	}
	return true;
}

u32 YWPANEL_FP_GetTime(void)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_GETTIME;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return false;
	}
	return data.data.time.second;
}

int YWPANEL_FP_SetPowerOnTime(u32 value)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_SETPOWERONTIME;
	data.data.time.second = value;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
	}
	return true;
}

u32 YWPANEL_FP_GetPowerOnTime(void)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_GETPOWERONTIME;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		return 0;
	}
	return data.data.time.second;
}

int YWPANEL_FP_GetKey(int blue, int key_nr, u32 *k)
{
	if (key_nr > -1 && key_nr < 5)
	{
		YWPANEL_FPData_t data;
		memset(&data, 0, sizeof(YWPANEL_FPData_t));
		data.dataType = (blue ? YWPANEL_DATATYPE_GETBLUEKEY1 : YWPANEL_DATATYPE_GETSTBYKEY1) + key_nr;
		if (YWPANEL_FP_SendData(&data))
		{
			*k = data.data.stbyKey.key;
			return true;
		}
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at [%d]",__LINE__);
	}
	return false;
}

int YWPANEL_FP_SetKey(int blue, int key_nr, u32 k)
{
	if (key_nr > -1 && key_nr < 5)
	{
		YWPANEL_FPData_t data;
		memset(&data, 0, sizeof(YWPANEL_FPData_t));
		data.dataType = (blue ? YWPANEL_DATATYPE_SETBLUEKEY1 : YWPANEL_DATATYPE_SETSTBYKEY1) + key_nr;
		data.data.stbyKey.key = k;
		if(YWPANEL_FP_SendData(&data))
		{
			return true;
		}
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at [%d]",__LINE__);
	}
	msleep(100); // Looks like the controller is returning junk data. Wait a couple of milliseconds, or the next i2c response will be messed up --martii
	return false;
}

int YWPANEL_FP_ControlTimer(int on)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_CONTROLTIMER;
	data.data.TimeState.startFlag = on;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]",__LINE__);
		return false;
	}
	return true;
}

int YWPANEL_FP_SetLed(int which, int on)
{
	int ErrorCode = 0;
	YWPANEL_FPData_t data;
	static u8 ledValue = 0;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));

	data.dataType = YWPANEL_DATATYPE_LBD;

	switch (which)
	{
		case LED_RED:
		{
			if (on)
			{
				ledValue |= YWPANEL_LBD_TYPE_POWER;
			}
			else
			{
				ledValue &= ~(YWPANEL_LBD_TYPE_POWER);
			}
			break;
		}
#if defined(SPARK)
		case LED_GREEN:
		{
			if (on)
			{
				ledValue |= YWPANEL_LBD_TYPE_SIGNAL;
			}
			else
			{
				ledValue &= ~(YWPANEL_LBD_TYPE_SIGNAL);
			}
			break;
		}
#endif
	}
	data.data.lbdData.value = ledValue;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
		ErrorCode = -ETIME;
	}
	return ErrorCode;
}

int YWPANEL_FP_SetPIOMode(PIO_Mode_T Mode_PIO)
{
	int ST_ErrCode = 0;

	if (Mode_PIO == PIO_Out)
	{
		stpio_configure_pin(pio_sda, STPIO_OUT);
	}
	else if (Mode_PIO == PIO_In)
	{
		stpio_configure_pin(pio_sda, STPIO_IN);
	}
	stpio_configure_pin(pio_scl, STPIO_OUT);
	stpio_configure_pin(pio_cs,  STPIO_OUT);

	return ST_ErrCode;
}

int YWPANEL_FP_WR(u8 data)
{
	int ErrorCode = 0;
	int i;

	down_write(&vfd_rws);
	for (i = 0; i < 8; i++)
	{
		stpio_set_pin(pio_scl, 0);
		udelay(1);
		stpio_set_pin(pio_sda, data & 0x01);
		stpio_set_pin(pio_scl, 1);
		udelay(1);
		data >>= 1;
	}
	up_write(&vfd_rws);
	return ErrorCode;
}

u8 YWPANEL_FP_RD(void)
{
	int ret = 0;
	int i;
	u8 val = 0;

	down_read(&vfd_rws);

	YWPANEL_FP_SetPIOMode(PIO_In);
	for (i = 0; i < 8; i++)
	{
		val >>= 1;
		stpio_set_pin(pio_scl, 0);
		udelay(1);

		ret = stpio_get_pin(pio_sda);
		if (ret)
		{
			val |= 0x80;
		}
		stpio_set_pin(pio_scl, 1);
		udelay(1);
	}
	udelay(1);
	YWPANEL_FP_SetPIOMode(PIO_Out);
	up_read(&vfd_rws);
	return val;
}

int YWPANEL_VFD_SegDigSeg(u8 dignum, SegNum_T segnum, u8 val)
{
	int ST_ErrCode = 0;
	u8 addr = 0;
	if (segnum < 0 && segnum > 1)
	{
		ST_ErrCode = -EINVAL;
	}
	FP_CS_CLR();
	if (segnum == SEGNUM1)
	{
		addr = VfdSegAddr[dignum].Segaddr1;
		VfdSegAddr[dignum].CurrValue1 = val;
	}
	else if (segnum == SEGNUM2)
	{
		addr = VfdSegAddr[dignum].Segaddr2;
		VfdSegAddr[dignum].CurrValue2 = val;
	}
	ST_ErrCode = YWPANEL_FP_WR(addr);
	udelay(10);
	ST_ErrCode = YWPANEL_FP_WR(val);
	FP_CS_SET();
	return ST_ErrCode;
}

int YWPANEL_FP_SetMode(FPMode_T mode)
{
	int ST_ErrCode = 0;
	u8 data = 0;

	if (mode == FPWRITEMODE)
	{
		data = 0x44;
		FP_CS_CLR();
		ST_ErrCode = YWPANEL_FP_WR(data);
		FP_CS_SET();
	}
	else if (mode == FPREADMODE)
	{
		data = 0x46;
		ST_ErrCode = YWPANEL_FP_WR(data);
		udelay(5);
	}
	return ST_ErrCode;
}

/* Show Content */
int YWPANEL_VFD_ShowContent_Standby(void)
{
	int ST_ErrCode = 0;
	YWPANEL_FPData_t data;
	if (down_interruptible(&vfd_sem))
	{
	   ST_ErrCode = -EBUSY;
	   return ST_ErrCode;
	}
	data.dataType = YWPANEL_DATATYPE_VFD;
	data.data.vfdData.type = YWPANEL_VFD_SETTING;
	data.data.vfdData.setValue = 0x8F; //set bits 3 and 7 and brightness 7
	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"ShowContent: error!\n");
		ST_ErrCode = -ETIME;
	}
	up(&vfd_sem);
	return ST_ErrCode;
}

int YWPANEL_VFD_ShowContent_Common(void)
{
	int ST_ErrCode = 0;
	FP_CS_CLR();
	ST_ErrCode = YWPANEL_FP_WR(0x8F);
	FP_CS_SET();
	return ST_ErrCode;
}

/* Show Content Off */
int YWPANEL_VFD_ShowContentOff_Standby(void)
{
	int ST_ErrCode = 0;
	YWPANEL_FPData_t data;
	if (down_interruptible(&vfd_sem))
	{
	   ST_ErrCode = -EBUSY;
	   return ST_ErrCode;
	}
	data.dataType = YWPANEL_DATATYPE_VFD;
	data.data.vfdData.type = YWPANEL_VFD_SETTING;
	data.data.vfdData.setValue = 0x87; //switch display off (bit 3 = 0)
	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"ShowContentOff: error!\n");
		ST_ErrCode = -ETIME;
	}
	up(&vfd_sem);
	return ST_ErrCode;
}

int YWPANEL_VFD_ShowContentOff_Common(void)
{
	int ST_ErrCode = 0;
	ST_ErrCode = YWPANEL_FP_WR(0x87); //switch display off (bit 3 = 0)

	return ST_ErrCode;
}

void YWPANEL_FP_ClearAll(void)
{
	int i;
	for (i = 0; i < 13; i++) // 13 digits?
	{
		YWPANEL_VFD_SegDigSeg(i + 1,SEGNUM1,0x00);
		VfdSegAddr[i + 1].CurrValue1 = 0x00;
		YWPANEL_VFD_SegDigSeg(i + 1,SEGNUM2,0x00);
		VfdSegAddr[i + 1].CurrValue2 = 0;
	}
}

void YWPANEL_VFD_DrawNum(u8 c, u8 position)
{
	int dignum;

	if (position < 1 || position > 4)
	{
		PANEL_PRINT((TRACE_ERROR, "Digit position error! %d\n", position));
		return;
	}
	if (c >	9)
	{
		PANEL_PRINT((TRACE_ERROR, "Number out of range!\n"));
		return;
	}
	dignum = 10 - position / 3;
	if (position % 2 == 1)
	{
		if (NumLib[c][1] & 0x01)
		{
			YWPANEL_VFD_SegDigSeg(dignum, SEGNUM1, VfdSegAddr[dignum].CurrValue1 | 0x80);
		}
		else
		{
			YWPANEL_VFD_SegDigSeg(dignum, SEGNUM1, VfdSegAddr[dignum].CurrValue1 & 0x7F);
		}
		VfdSegAddr[dignum].CurrValue2 = VfdSegAddr[dignum].CurrValue2 & 0x40;
		YWPANEL_VFD_SegDigSeg(dignum, SEGNUM2, (NumLib[c][1] >> 1) | VfdSegAddr[dignum].CurrValue2);
	}
	else
	{
		if ((NumLib[c][0] & 0x01))
		{
			YWPANEL_VFD_SegDigSeg(dignum, SEGNUM2, VfdSegAddr[dignum].CurrValue2 | 0x40);
		}
		else
		{
			YWPANEL_VFD_SegDigSeg(dignum, SEGNUM2, VfdSegAddr[dignum].CurrValue2 & 0x3F);
		}
		VfdSegAddr[dignum].CurrValue1 = VfdSegAddr[dignum].CurrValue1 & 0x80;
		YWPANEL_VFD_SegDigSeg(dignum, SEGNUM1, (NumLib[c][0] >>1 ) | VfdSegAddr[dignum].CurrValue1 );
	}
}

void YWPANEL_VFD_Seg_Addr_Init(void)
{
	u8 i, addr = 0xC0;  //address flag
	for (i = 0; i < 13; i++) // 13 digits?
	{
		VfdSegAddr[i + 1].CurrValue1 = 0;
		VfdSegAddr[i + 1].CurrValue2 = 0;
		VfdSegAddr[i + 1].Segaddr1 = addr;
		VfdSegAddr[i + 1].Segaddr2 = addr + 1;
		addr += 3;
	}
}

/* Show Time */
static int YWPANEL_VFD_ShowTime_StandBy(u8 hh, u8 mm)
{
	int ErrorCode = 0;
	YWPANEL_FPData_t data;
	u8 digitNum1, digitNum2, temp;
	if (down_interruptible(&vfd_sem))
	{
	   return -EBUSY;
	}
//show hour
	{
		memset(&data, 0, sizeof(YWPANEL_FPData_t));

		data.dataType = YWPANEL_DATATYPE_VFD;

		digitNum2 = YWPANEL_CharArray[hh / 10];
		digitNum1 = YWPANEL_CharArray[hh % 10];

		temp = digitNum2;
		digitNum2 = (digitNum2 & 0xbf) | (digitNum1 & 0x40);
		digitNum1 = (digitNum1 & 0x3c) | ((temp & 0x40) << 1) | ((digitNum1 & 0x01) << 1)|((digitNum1 & 0x02) >> 1);
		data.data.vfdData.type = YWPANEL_VFD_DISPLAY;
		data.data.vfdData.address[0] = VfdSegAddr[10].Segaddr2;

		data.data.vfdData.DisplayValue[0] = digitNum2;
		VfdSegAddr[10].CurrValue2 = data.data.vfdData.DisplayValue[0];

		if (YWPANEL_FP_SendData(&data) != true)
		{
			ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
			ErrorCode = -ETIME;
		}

		data.data.vfdData.address[0] = VfdSegAddr[10].Segaddr1;

		data.data.vfdData.DisplayValue[0] = digitNum1;
		VfdSegAddr[10].CurrValue1= data.data.vfdData.DisplayValue[0];

		if (YWPANEL_FP_SendData(&data) != true)
		{
			ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
			ErrorCode = -ETIME;
		}
	}
//show minute
	{ // code the same as hours except VfdSegAddr index?
		memset(&data, 0, sizeof(YWPANEL_FPData_t));
		data.dataType = YWPANEL_DATATYPE_VFD;
		digitNum2 = YWPANEL_CharArray[mm / 10];
		digitNum1 = YWPANEL_CharArray[mm % 10];

		temp = digitNum2;
		digitNum2 = (digitNum2 &0xbf) | (digitNum1 & 0x40);
		digitNum1 = (digitNum1 &0x3c) | ((temp & 0x40) << 1) | ((digitNum1 & 0x01) << 1) | ((digitNum1 & 0x02) >>1);
		data.data.vfdData.type = YWPANEL_VFD_DISPLAY;
		data.data.vfdData.address[0] = VfdSegAddr[9].Segaddr2;

		data.data.vfdData.DisplayValue[0] = digitNum2;
		VfdSegAddr[9].CurrValue2 = data.data.vfdData.DisplayValue[0];

		if (YWPANEL_FP_SendData(&data) != true)
		{
			ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
			ErrorCode = -ETIME;
		}

		data.data.vfdData.address[0] = VfdSegAddr[9].Segaddr1;

		data.data.vfdData.DisplayValue[0] = digitNum1;
		VfdSegAddr[9].CurrValue1= data.data.vfdData.DisplayValue[0];

		if (YWPANEL_FP_SendData(&data) != true)
		{
			ywtrace_print(TRACE_ERROR,"YWPANEL_FP_SendData failed at line [%d]\n",__LINE__);
			ErrorCode = -ETIME;
		}
	}
	up(&vfd_sem);
	return ErrorCode;
}

static int YWPANEL_VFD_ShowTime_Common(u8 hh, u8 mm)
{
	int ErrorCode = 0;
	if (down_interruptible(&vfd_sem))
	{
	   ErrorCode = -EBUSY;
	   return ErrorCode;
	}
	if ((hh > 23) && (mm > 59)) // BUG fixed: 24h and 60m were valid!
	{
		ErrorCode = -EINVAL;
	}
	YWPANEL_VFD_DrawNum((hh / 10), 1);
	YWPANEL_VFD_DrawNum((hh % 10), 2);
	YWPANEL_VFD_DrawNum((mm / 10), 3);
	YWPANEL_VFD_DrawNum((mm % 10), 4);
	up(&vfd_sem);
	return ErrorCode;
}

/* Show Time Off */
//FIXME: does not switch display off: sets display time to midnight!
static int YWPANEL_VFD_ShowTimeOff_StandBy(void)
{
	return YWPANEL_FP_ShowTime(0,0);
}

static int YWPANEL_VFD_ShowTimeOff_Common(void)
{
	int ST_ErrCode = 0;

	if (down_interruptible(&vfd_sem))
	{
		ST_ErrCode =-EBUSY;
		return ST_ErrCode;
	}
	ST_ErrCode = YWPANEL_VFD_SegDigSeg(9, SEGNUM1, 0x00);
	ST_ErrCode = YWPANEL_VFD_SegDigSeg(9, SEGNUM2, 0x00);
	ST_ErrCode = YWPANEL_VFD_SegDigSeg(10, SEGNUM1, 0x00);
	ST_ErrCode = YWPANEL_VFD_SegDigSeg(10, SEGNUM2, 0x00);
	up(&vfd_sem);
	return ST_ErrCode;
}

/* Set Brightness */
int YWPANEL_VFD_SetBrightness_StandBy(int level)
{
	int ST_ErrCode = 0;
	YWPANEL_FPData_t data;

	if (down_interruptible(&vfd_sem))
	{
		ST_ErrCode = -EBUSY;
		return ST_ErrCode;
	}
	if (level < 0)
	{
		level = 0;
	}
	else if (level > 7)
	{
		level = 7;
	}
	data.dataType = YWPANEL_DATATYPE_VFD;
	data.data.vfdData.type = YWPANEL_VFD_SETTING;
	data.data.vfdData.setValue = level | 0x88; //used to be 0x78
	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"SetBrightness: error!\n");
		ST_ErrCode = -ETIME;
	}
	up(&vfd_sem);
	return ST_ErrCode;
}

static int YWPANEL_VFD_SetBrightness_Common(int level)
{
	int ST_ErrCode = 0;

	if (level < 0)
	{
		level = 0;
	}
	else if (level > 7)
	{
		level = 7;
	}
	FP_CS_CLR();
	YWPANEL_FP_WR (0x88 | level); //used to be 0x78
	FP_CS_SET();
	return ST_ErrCode;
}

/* Scan Keyboard */
static u8 YWPANEL_FP_ScanKeyboard_StandBy(void)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(data));

	switch (panel_disp_type)
	{
		case YWPANEL_FP_DISPTYPE_VFD:
		{
			data.dataType = YWPANEL_DATATYPE_VFD;
			data.data.vfdData.type = YWPANEL_VFD_READKEY;
			break;
		}
		case YWPANEL_FP_DISPTYPE_DVFD:
		case YWPANEL_FP_DISPTYPE_LED:
		{
			data.dataType = YWPANEL_DATATYPE_SCANKEY;
			break;
		}
		case YWPANEL_FP_DISPTYPE_LCD:
		case YWPANEL_FP_DISPTYPE_LBD:
		default:
		{
			break;
		}
	}

	if (YWPANEL_FP_SendData(&data) == true)
	{
		switch (panel_disp_type)
		{
			case YWPANEL_FP_DISPTYPE_VFD:
			{
				return data.data.vfdData.key;
			}
			case YWPANEL_FP_DISPTYPE_DVFD:
			case YWPANEL_FP_DISPTYPE_LED:
			{
				return data.data.ScanKeyData.keyValue;
			}
			case YWPANEL_FP_DISPTYPE_LCD:
			case YWPANEL_FP_DISPTYPE_LBD:
			default:
			{
				break;
			}
		}
	}
	else
	{
		printk("YWPANEL_FP_SendData Error\n");
	}
	return INVALID_KEY;
}

static u8 YWPANEL_FP_ScanKeyboard_Unknown(void)
{
	return INVALID_KEY;
}

static u8 YWPANEL_FP_ScanKeyboard_Common(void)
{
	int ST_ErrCode = 0;
	u8 key_val[6];
	u8 i = 0;

	FP_CS_CLR();
	ST_ErrCode = YWPANEL_FP_SetMode(FPREADMODE);
	if (ST_ErrCode != 0)
	{
		PANEL_DEBUG(ST_ErrCode);
		return INVALID_KEY;
	}

	for (i = 0; i < 6; i++)
	{
		key_val[i] = YWPANEL_FP_RD();
	}
	FP_CS_SET();

	ST_ErrCode = YWPANEL_FP_SetMode(FPWRITEMODE);
	if (ST_ErrCode != 0)
	{
		PANEL_DEBUG(ST_ErrCode);
		return INVALID_KEY;
	}
	return key_val[5];
}

int YWPANEL_FP_GetKeyValue(void)
{
	int byte = 0;
	int key_val = INVALID_KEY;

	if (down_interruptible(&vfd_sem))
	{
		return key_val;
	}

	switch (YWFP_INFO.fp_type)
	{
		case YWFP_STAND_BY:
		case YWFP_COMMON:
		{
			byte = YWPANEL_FP_ScanKeyboard();
//			break;
		}
		default:
		{
			break;
		}
	}

	switch (byte)
	{
		case 0x01:
		{
			key_val = KEY_EXIT;
			break;
		}
		case 0x02:
		{
			key_val = KEY_LEFT;
			break;
		}
		case 0x04:
		{
			key_val = KEY_UP;
			break;
		}
		case 0x08:
		{
			key_val = KEY_OK;
			break;
		}
		case 0x10:
		{
			key_val = KEY_RIGHT;
			break;
		}
		case 0x20:
		{
			key_val = KEY_DOWN;
			break;
		}
		case 0x40:
		{
			key_val = KEY_POWER;
			break;
		}
		case 0x80:
		{
			key_val = KEY_MENU;
			break;
		}
		default:
		{
			PANEL_PRINT((TRACE_ERROR,"Key 0x%s is INVALID\n",byte));
		}
		case 0x00:
		{
			key_val = INVALID_KEY;
			break;
		}
	}
	up(&vfd_sem);
	return key_val;
}

/* DVFD Show String */
#if defined(SPARK7162)
// Code for dot VFD
static bool bTimeMode = true;
static char strDvfd[16][5];

void YWVFDi_DVFDCleanChar(u8 i)
{
	u8 j;
	int off = 0;

	if (i >= YWPANEL_MAX_DVFD_LENGTH)
	{
	    return;
	}
	if (bTimeMode && i >= 10)
	{
	    return;
	}

	if (bTimeMode)
	{
		off = 6;
	}

	for (j = 0; j < 5; j++)
	{
		strDvfd[i + off][j] = 0;
	}
}

void YWVFDi_DVFDFillAsciiChar(u8 i, int iChar)
{
	u8 j;
	int off = 0;

	if (i >= YWPANEL_MAX_DVFD_LENGTH)
	{
	    return;
	}

	if ((bTimeMode) && (i >= 10))  //if clock display
	{
	    return;
	}

	if (bTimeMode)
	{
		off = 6;
	}

	for (j = 0; j < 5; j++)
	{
		strDvfd[i + off][j] = dvfd_bitmap[iChar][j];
	}
}

void YWVFDi_DVFDFillChar(u8 i, u8 c)
{
	if ((c >= ' ') && (c <= '~'))
	{
		YWVFDi_DVFDFillAsciiChar(i, c - ' ');
	}
	else
	{
		YWVFDi_DVFDCleanChar(i);
	}
}

static void YWVFDi_DVFDFillString(char* str)
{
	int number_of_utf8_characters = utf8strlen(str, strlen(str));
	int i;

	if (number_of_utf8_characters > YWPANEL_MAX_DVFD_LENGTH - 1)
	{
		number_of_utf8_characters = YWPANEL_MAX_DVFD_LENGTH;
	}

	for (i = 0; i < number_of_utf8_characters; i++)
	{
		int size = utf8charlen(*str);
		if (size == 1)
		{
			YWVFDi_DVFDFillChar(i, *str);
		}
		else
		{
			YWVFDi_DVFDCleanChar(i);
		}
		str += size;
	}

	for(; i < YWPANEL_MAX_DVFD_LENGTH; i++)
	{
		YWVFDi_DVFDCleanChar(i);
	}
}

int YWVFDi_DVFDDisplaySync(void)
{
	int ret = 0;
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));

	data.dataType = YWPANEL_DATATYPE_DVFD;
	data.data.dvfdData.type = YWPANEL_DVFD_DISPLAYSYNC;

	if (YWPANEL_FP_SendData(&data) != true)
	{
		//printf("DVFD display sync has an error!\n");
		ret = -2;
	}
	return ret;
}

int YWVFDi_DVFDSendString(void)
{
	int ret = 0;
	u8 i, j;
	u8 address;
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));

	data.dataType = YWPANEL_DATATYPE_DVFD;
	data.data.dvfdData.type = YWPANEL_DVFD_DISPLAYSTRING;
	for (i = 0; i < 4; i++)
	{
		data.data.dvfdData.ulen = 4;
		for (j = 0; j < 4; j++)
		{
			address = i * 4 + j;
			data.data.dvfdData.address[j] = address;
			data.data.dvfdData.DisplayValue[j][0] = strDvfd[address][0];
			data.data.dvfdData.DisplayValue[j][1] = strDvfd[address][1];
			data.data.dvfdData.DisplayValue[j][2] = strDvfd[address][2];
			data.data.dvfdData.DisplayValue[j][3] = strDvfd[address][3];
			data.data.dvfdData.DisplayValue[j][4] = strDvfd[address][4];
		}
		YWVFD_Debug("%s:%d\n", __func__, __LINE__);
		if (YWPANEL_FP_SendData(&data) != true)
		{
			printk("%s:%d: YWPANEL_FP_SendData() failed\n", __func__, __LINE__);
			ret = -2;
		}
	}
	return ret;
}

int YWVFDi_DVFDDisplayString(void)
{
	int ret = YWVFDi_DVFDSendString();

	YWVFD_Debug("%s:%d\n", __func__, __LINE__);

	ret = YWVFDi_DVFDDisplaySync();

	return ret;
}

int YWPANEL_DVFD_ShowString_Standby(char* str)
{
	int ret = 0;

	YWVFD_Debug("%s:%d\n", __FUNCTION__, __LINE__);

	YWVFDi_DVFDFillString(str);

	YWVFD_Debug("%s:%d\n", __FUNCTION__, __LINE__);

	ret = YWVFDi_DVFDDisplayString();

	return ret;
 }

#if 0
bool YWPANEL_FP_DvfdSetTimeMode(bool on)
{
	 YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_DVFD;
	data.data.dvfdData.type = YWPANEL_DVFD_SETTIMEMODE;
	data.data.dvfdData.setValue = on;

	if (YWPANEL_FP_SendData(&data) != true)
	{
	return false;
	}

	bTimeMode = on;
	return true;
}
#endif

bool YWPANEL_FP_DvfdGetTimeMode(bool *pOn)
{
	YWPANEL_FPData_t data;

	memset(&data, 0, sizeof(YWPANEL_FPData_t));
	data.dataType = YWPANEL_DATATYPE_DVFD;
	data.data.dvfdData.type = YWPANEL_DVFD_GETTIMEMODE;

	if (YWPANEL_FP_SendData(&data) != true)
	{
	return false;
	}

	(*pOn) = data.data.dvfdData.setValue;
	bTimeMode = data.data.dvfdData.setValue;

	return true;
}
//end code for dot VFD
#endif // defined(SPARK(7162)

/* LED Show String */
//lwj add begin for LED panel
#if defined(SPARK)
static u8 YWPANEL_LedDisplayData[YWPANEL_MAX_LED_LENGTH];

static void YWPANEL_LEDSetString(char *str)
{
	int i;
	int number_of_utf8_characters = utf8strlen(str, strlen(str));

	for (i = 0; i < YWPANEL_MAX_LED_LENGTH; i++)
	{
		YWPANEL_LedDisplayData[i] = 0;
		if (i < number_of_utf8_characters)
		{
			int size = utf8charlen(*str);
			if (size == 1)
			{
				YWPANEL_LedDisplayData[i] = ywpanel_led_map[(unsigned char) *str];
				if ((i == 1) && ((*(str + 1) == '.') || (*(str + 1) == ',') || (*(str + 1) == ':')) && !(YWPANEL_LedDisplayData[i] & 1))
				{
					YWPANEL_LedDisplayData[i] |= 1;
					str++;
					number_of_utf8_characters--;
				}
			}
			str += size;
		}
	}
}

static int YWPANEL_LEDDisplayString(void)
{
	int ret = 0;
	YWPANEL_FPData_t data;

	data.dataType = YWPANEL_DATATYPE_LED;

	data.data.ledData.led1 = YWPANEL_LedDisplayData[0];
	data.data.ledData.led2 = YWPANEL_LedDisplayData[1];
	data.data.ledData.led3 = YWPANEL_LedDisplayData[2];
	data.data.ledData.led4 = YWPANEL_LedDisplayData[3];

	if (YWPANEL_FP_SendData(&data)!= true)
	{
		ret = -1;
		ywtrace_print(TRACE_ERROR, "[ERROR][%s] TIME OUT\n",__func__);
 	}
	return ret;
}

static int YWPANEL_LED_ShowString_Standby(char *str)
{
	YWPANEL_FP_ControlTimer(false);
	YWPANEL_LEDSetString(str);
	return YWPANEL_LEDDisplayString();
}
//lwj add end
#endif //defined(SPARK)

/* VFD Show String */
static int lookup_utf8(unsigned char *str, int *v1, int *v2)
{ // str is guaranteed to start with a valid UTF-8 character, no UTF-8 validation required

	int size = utf8charlen(*str);
	*v1 = *v2 = 0;

	// Plain ASCII
	if (size == 1)
	{
		u8 c = (int)*str - 0x20;  //remove ctrl-character offset
		*v1 = CharLib[c][0];
		*v2 = CharLib[c][1];
		return size;
	}

	// UTF+0400 to 047F
	switch (*str)
	{
		case 0xd0:
		{
			str++;
			*v1 = UTF_D0[*str & 0x3f][0];
			*v2 = UTF_D0[*str & 0x3f][1];
			break;
		}
		case 0xd1:
		{
			str++;
			*v1 = UTF_D1[*str & 0x3f][0];
			*v2 = UTF_D1[*str & 0x3f][1];
			break;
		}
	}
	return size;
}

static int YWPANEL_VFD_ShowString_StandBy(char* str)
{
	int ST_ErrCode = 0 ;
	int number_of_utf8_characters, i;

	YWPANEL_FPData_t data;

	if (down_interruptible(&vfd_sem))
	{
	   ST_ErrCode =-EBUSY;
	   return ST_ErrCode;
	}

	number_of_utf8_characters = utf8strlen(str, strlen(str));
	data.dataType = YWPANEL_DATATYPE_VFD;
	for (i = 0; i < YWPANEL_MAX_VFD_LENGTH; i++)
	{
		int v1 = 0, v2 = 0;
		data.data.vfdData.type = YWPANEL_VFD_DISPLAYSTRING;

		if (number_of_utf8_characters)
		{
			int usedbytes = lookup_utf8((unsigned char *)str, &v1, &v2);
			if (usedbytes)
			{
				number_of_utf8_characters--;
				str += usedbytes;
			}
			else
			{
				number_of_utf8_characters = 0;
			}
		}

		VfdSegAddr[i + 1].CurrValue1 = v1;
		VfdSegAddr[i + 1].CurrValue2 = v2;

		data.data.vfdData.address[2 * i] = VfdSegAddr[i + 1].Segaddr1;
		data.data.vfdData.DisplayValue[2 * i] = VfdSegAddr[i + 1].CurrValue1;
		data.data.vfdData.address[2 * i + 1] = VfdSegAddr[i + 1].Segaddr2;
		data.data.vfdData.DisplayValue[2 * i + 1] = VfdSegAddr[i + 1].CurrValue2;
	}

	if (YWPANEL_FP_SendData(&data) != true)
	{
		PANEL_DEBUG("Error in VFD show string!\n");
		ST_ErrCode = -ETIME;
	}
	up(&vfd_sem);
	return ST_ErrCode;
}

static int YWPANEL_VFD_ShowString_Common(char* str)
{
	int ST_ErrCode = 0;
	int number_of_utf8_characters, i;

	if (down_interruptible(&vfd_sem))
	{
	   ST_ErrCode =-EBUSY;
	   return ST_ErrCode;
	}

	number_of_utf8_characters = utf8strlen(str, strlen(str));
	for (i = 1; i < YWPANEL_MAX_VFD_LENGTH + 1; i++)
	{
		int v1 = 0, v2 = 0;

		if (number_of_utf8_characters)
		{
			int usedbytes = lookup_utf8((unsigned char *)str, &v1, &v2);
			if (usedbytes)
			{
				number_of_utf8_characters--;
				str += usedbytes;
			}
			else
			{
				number_of_utf8_characters = 0;
			}
		}

		YWPANEL_VFD_SegDigSeg(i, SEGNUM1, v1);
		YWPANEL_VFD_SegDigSeg(i, SEGNUM2, v2);
	}

	up(&vfd_sem);
	return ST_ErrCode;
}

/* Show Icon */
static int YWPANEL_VFD_ShowIcon_StandBy(int which, int on)
{
	int ST_ErrCode = 0;
	int dig_num = 0, seg_num = 0;
	SegNum_T seg_part = 0;
	u8 seg_offset = 0;
	YWPANEL_FPData_t data;

	data.dataType = YWPANEL_DATATYPE_VFD;

	if (down_interruptible(&vfd_sem))
	{
		return -EBUSY;
	}

	if (which < VICON_FIRST || which > VICON_LAST)
	{
		PANEL_DEBUG(ST_ErrCode);
		up(&vfd_sem);
		return -EINVAL;
	}

	dig_num = which / 16;
	seg_num = which % 16;
	seg_part = seg_num / 9;

	data.data.vfdData.type = YWPANEL_VFD_DISPLAY;

	if (seg_part == SEGNUM1)
	{
		seg_offset = 0x01 << ((seg_num % 9) - 1);
		data.data.vfdData.address[0] = VfdSegAddr[dig_num].Segaddr1;
		if (on)
		{
			VfdSegAddr[dig_num].CurrValue1 |= seg_offset;
		}
		else
		{
			VfdSegAddr[dig_num].CurrValue1 &= ~(seg_offset);
		}
		data.data.vfdData.DisplayValue[0] = VfdSegAddr[dig_num].CurrValue1;
	}
	else if (seg_part == SEGNUM2)
	{
		seg_offset = 0x01 << ((seg_num % 8) - 1);
		data.data.vfdData.address[0] = VfdSegAddr[dig_num].Segaddr2;
		if (on)
		{
			VfdSegAddr[dig_num].CurrValue2 |= seg_offset;
		}
		else
		{
			VfdSegAddr[dig_num].CurrValue2 &= ~(seg_offset);
		}
		data.data.vfdData.DisplayValue[0] = VfdSegAddr[dig_num].CurrValue2;
	}
	if (YWPANEL_FP_SendData(&data) != true)
	{
		ywtrace_print(TRACE_ERROR,"Error showing an icon!\n");
		ST_ErrCode = -ETIME;
	}

	up(&vfd_sem);
	return ST_ErrCode;
}

static int YWPANEL_VFD_ShowIcon_Common(int which, int on)
{
	int ST_ErrCode = 0;
	int dig_num = 0, seg_num = 0;
	SegNum_T seg_part = 0;
	u8 seg_offset = 0;
	u8 addr = 0, val = 0;

	if (down_interruptible(&vfd_sem))
	{
		return -EBUSY;
	}

	if (which < VICON_FIRST || which > VICON_LAST)
	{
		PANEL_DEBUG(ST_ErrCode);
		up(&vfd_sem);
		return -EINVAL;
	}
	dig_num = which / 16;
	seg_num = which % 16;
	seg_part = seg_num / 9;

	FP_CS_CLR();
	if (seg_part == SEGNUM1)
	{
		seg_offset = 0x01 << ((seg_num % 9) - 1);
		addr = VfdSegAddr[dig_num].Segaddr1;
		if (on)
		{
			VfdSegAddr[dig_num].CurrValue1 |= seg_offset;
		}
		else
		{
			VfdSegAddr[dig_num].CurrValue1 &= ~(seg_offset);
		}
		val = VfdSegAddr[dig_num].CurrValue1;
	}
	else if (seg_part == SEGNUM2)
	{
		seg_offset = 0x01 << ((seg_num % 8) - 1);
		addr = VfdSegAddr[dig_num].Segaddr2;
		if (on)
		{
			VfdSegAddr[dig_num].CurrValue2 |= seg_offset;
		}
		else
		{
			VfdSegAddr[dig_num].CurrValue2 &= ~(seg_offset);
		}
		val = VfdSegAddr[dig_num].CurrValue2;
	}
	ST_ErrCode = YWPANEL_FP_WR(addr);
	udelay(10);
	ST_ErrCode = YWPANEL_FP_WR(val);
	FP_CS_SET();
	up(&vfd_sem);
	return ST_ErrCode;
}

#ifdef CONFIG_CPU_SUBTYPE_STX7105
static int YWPANEL_FP_DETECT(void)
{
	int ret = 0;
	softi2c_init();
	if (softi2c_online())
	{
		YWFP_INFO.fp_type = YWFP_STAND_BY;
		ret = 0;
	}
	else
	{
		YWFP_INFO.fp_type = YWFP_COMMON;
		ret = -EINVAL;
	}
	return ret;
}
#else
static int YWPANEL_FP_DETECT(void)
{
	int ret = 0;
	u8 localBuff[2] = {0xaa, 0xaa};

	struct i2c_msg i2c_msg = { .addr = I2C_BUS_ADD, .flags = 0, .buf = localBuff, .len = 2 };

	YWFP_INFO.fp_type = YWFP_UNKNOWN;

	panel_i2c_adapter = i2c_get_adapter(I2C_BUS_NUM);
	if (NULL == panel_i2c_adapter)
	{
		ywtrace_print(TRACE_ERROR,"i2c_get_adapter failed\n");
		return -ENODEV;
	}

	/* use i2c write to detect */
	ret = i2c_transfer(panel_i2c_adapter, &i2c_msg, 1);
	if (ret == 1)
	{
		YWFP_INFO.fp_type = YWFP_STAND_BY;
	}
	else
	{
		YWFP_INFO.fp_type = YWFP_COMMON;
	}
	return 0;
}
#endif

/* Initialize */
static int YWPANEL_FP_Init_StandBy(void)
{
	int ErrorCode = 0 ;
	init_MUTEX(&vfd_sem);
	init_MUTEX(&vfd_sem_rw);

	YWPANEL_VFD_Seg_Addr_Init();
//	YWPANEL_FP_ShowString("Welcome!");  //Main?
//	YWPANEL_FP_ShowString("bbbb"); //Clock (wrong) or LED?
	return ErrorCode;
}

static int YWPANEL_FP_Init_Common(void)
{
	int ErrorCode = 0 ;

	init_MUTEX(&vfd_sem);
	init_rwsem(&vfd_rws);

	pio_sda = stpio_request_pin(3,2, "pio_sda", STPIO_OUT);
	pio_scl = stpio_request_pin(3,4, "pio_scl", STPIO_OUT);
	pio_cs  = stpio_request_pin(3,5, "pio_cs",  STPIO_OUT);
	if (!pio_sda || !pio_scl || !pio_cs )
	{
		return -ENODEV;
	}
	stpio_set_pin(pio_scl, 1);
	stpio_set_pin(pio_cs,  1);

	FP_CS_CLR();
	ErrorCode = YWPANEL_FP_WR(0x0C);
	FP_CS_SET();

	ErrorCode = YWPANEL_FP_SetMode(FPWRITEMODE);
	YWPANEL_VFD_Seg_Addr_Init();
	YWPANEL_FP_ClearAll();
	//YWPANEL_FP_ShowContent();
	YWPANEL_FP_ShowString("Welcome!");

	return ErrorCode;
 }

/* Terminate */
static int YWPANEL_FP_Term_StandBy(void)
{
#ifdef CONFIG_CPU_SUBTYPE_STX7105
	softi2c_cleanup();
#else
	i2c_put_adapter(panel_i2c_adapter);
#endif
	return 0;
}

static int YWPANEL_FP_Term_Common(void)
{
#ifdef CONFIG_CPU_SUBTYPE_STX7105
	softi2c_cleanup();
#else
	i2c_put_adapter(panel_i2c_adapter);
#endif
	return 0;
}
/* Unknowns */
static int YWPANEL_FP_ShowTime_Unknown(u8 hh, u8 mm)
{
	return -ENODEV;
}

static int YWPANEL_FP_SetBrightness_Unknown(int level)
{
	return -ENODEV;
}

static int YWPANEL_FP_ShowString_Unknown(char* str)
{
	return -ENODEV;
}

static int YWPANEL_FP_ShowIcon_Unknown(int which, int on)
{
	return -ENODEV;
}

static int YWPANEL_FP_X_Unknown(void)
{
	return -ENODEV;
}

static int YWPANEL_FP_Init_Unknown(void)
{
	return 0;
}


int (*YWPANEL_FP_Initialize)(void);
int (*YWPANEL_FP_Term)(void);
int (*YWPANEL_FP_ShowIcon)(int, int);
int (*YWPANEL_FP_ShowTime)(u8 hh,u8 mm);
int (*YWPANEL_FP_ShowTimeOff)(void);
int (*YWPANEL_FP_SetBrightness)(int);
u8 (*YWPANEL_FP_ScanKeyboard)(void);
int (*YWPANEL_FP_ShowString)(char *);
int (*YWPANEL_FP_ShowContent)(void);
int (*YWPANEL_FP_ShowContentOff)(void);

int YWPANEL_width = YWPANEL_MAX_VFD_LENGTH;
YWPANEL_Version_t panel_version;

int YWPANEL_FP_Init(void)
{
	int ErrorCode = -ENODEV;
	int i, k;

	YWPANEL_FP_Initialize = YWPANEL_FP_Init_Unknown;
	YWPANEL_FP_Term = YWPANEL_FP_X_Unknown;
	YWPANEL_FP_ShowIcon = YWPANEL_FP_ShowIcon_Unknown;
	YWPANEL_FP_ShowTime = YWPANEL_FP_ShowTime_Unknown;
	YWPANEL_FP_ShowTimeOff = YWPANEL_FP_X_Unknown;
	YWPANEL_FP_SetBrightness = YWPANEL_FP_SetBrightness_Unknown;
	YWPANEL_FP_ScanKeyboard = YWPANEL_FP_ScanKeyboard_Unknown;
	YWPANEL_FP_ShowString = YWPANEL_FP_ShowString_Unknown;
	YWPANEL_FP_ShowContent = YWPANEL_FP_X_Unknown;
	YWPANEL_FP_ShowContentOff = YWPANEL_FP_X_Unknown;

	if (YWPANEL_FP_DETECT() != 0) // find front panel type
	{
		ywtrace_print(TRACE_ERROR, "FP type detect failed\n");
		return ErrorCode;
	}

	printk("FP type = %d\n", YWFP_INFO.fp_type);

	switch (YWFP_INFO.fp_type)
	{
		case YWFP_STAND_BY:
		{
			YWPANEL_FP_Initialize = YWPANEL_FP_Init_StandBy;
			YWPANEL_FP_Term = YWPANEL_FP_Term_StandBy;
			YWPANEL_FP_ScanKeyboard = YWPANEL_FP_ScanKeyboard_StandBy;
			break;
		}
		case YWFP_COMMON:
		{
			YWPANEL_FP_Initialize = YWPANEL_FP_Init_Common;
			YWPANEL_FP_Term = YWPANEL_FP_Term_Common;
			YWPANEL_FP_ScanKeyboard = YWPANEL_FP_ScanKeyboard_Common;
			break;
		}
		default:
		{
			return ErrorCode;
		}
	}

	ErrorCode = YWPANEL_FP_Initialize();

	memset(&panel_version, 0, sizeof(YWPANEL_Version_t));

	if (YWPANEL_FP_GetVersion(&panel_version)) // get FP version info
	{
		panel_disp_type = panel_version.DisplayInfo; // get display type

		if (panel_disp_type < YWPANEL_FP_DISPTYPE_UNKNOWN || panel_disp_type > YWPANEL_FP_DISPTYPE_LBD)
		{ // if display type invalid
			panel_disp_type = YWPANEL_FP_DISPTYPE_VFD; // default to VFD
		}

		switch (panel_disp_type)
		{
#if defined(SPARK7162)
			case YWPANEL_FP_DISPTYPE_DVFD:
			{
				bool bOn;
				YWPANEL_FP_DvfdGetTimeMode(&bOn);
				YWVFD_Debug("INIT\n");
				YWVFD_Debug("boot\n");

				YWPANEL_width = YWPANEL_MAX_DVFD_LENGTH;
				YWPANEL_FP_ShowIcon = YWPANEL_VFD_ShowIcon_StandBy;
				YWPANEL_FP_ShowTime = YWPANEL_VFD_ShowTime_StandBy;
				YWPANEL_FP_ShowTimeOff = YWPANEL_VFD_ShowTimeOff_StandBy;
				YWPANEL_FP_SetBrightness = YWPANEL_VFD_SetBrightness_StandBy;
				YWPANEL_FP_ShowString = YWPANEL_DVFD_ShowString_Standby;
				YWPANEL_FP_ShowContent = YWPANEL_VFD_ShowContent_Standby;
				YWPANEL_FP_ShowContentOff = YWPANEL_VFD_ShowContentOff_Standby;
				break;
			}
#elif defined(SPARK)
			case YWPANEL_FP_DISPTYPE_LED:
			{
				YWPANEL_width = YWPANEL_MAX_LED_LENGTH;
				YWPANEL_FP_ShowString = YWPANEL_LED_ShowString_Standby;
				break;
			}
#endif
			case YWPANEL_FP_DISPTYPE_VFD:
			default:
			{
//				YWPANEL_width = YWPANEL_MAX_VFD_LENGTH;
				if (YWFP_INFO.fp_type == YWFP_COMMON)
				{
					YWPANEL_FP_ShowIcon = YWPANEL_VFD_ShowIcon_Common;
					YWPANEL_FP_ShowTime = YWPANEL_VFD_ShowTime_Common;
					YWPANEL_FP_ShowTimeOff = YWPANEL_VFD_ShowTimeOff_Common;
					YWPANEL_FP_SetBrightness = YWPANEL_VFD_SetBrightness_Common;
					YWPANEL_FP_ShowString = YWPANEL_VFD_ShowString_Common;
					YWPANEL_FP_ShowContent = YWPANEL_VFD_ShowContent_Common;
					YWPANEL_FP_ShowContentOff = YWPANEL_VFD_ShowContentOff_Common;
				}
				else //if (YWFP_INFO.fp_type == YWFP_STAND_BY)
				{
					YWPANEL_FP_ShowIcon = YWPANEL_VFD_ShowIcon_StandBy;
					YWPANEL_FP_ShowTime = YWPANEL_VFD_ShowTime_StandBy;
					YWPANEL_FP_ShowTimeOff = YWPANEL_VFD_ShowTimeOff_StandBy;
					YWPANEL_FP_SetBrightness = YWPANEL_VFD_SetBrightness_StandBy;
					YWPANEL_FP_ShowString = YWPANEL_VFD_ShowString_StandBy;
					YWPANEL_FP_ShowContent = YWPANEL_VFD_ShowContent_Standby;
					YWPANEL_FP_ShowContentOff = YWPANEL_VFD_ShowContentOff_Standby;
				}
				break;
			}
		}

		printk("FP CPU type   : %d\n", panel_version.CpuType);
		printk("Display type  : %d\n", panel_version.DisplayInfo);
		printk("# of FP keys  : %d\n", panel_version.scankeyNum);
		printk("FP SW Version : %d.%d\n", panel_version.swMajorVersion, panel_version.swSubVersion);

		for (i = 0; i < 5; i++)
		{
			if (YWPANEL_FP_GetKey(0, i, &k))
			{
				printk ("Standby key %d = %.8x\n", i, k);
			}
		}
		for (i = 0; i < 5; i++)
		{
			if (YWPANEL_FP_GetKey(1, i, &k))
			{
				printk ("Blue key %d = %.8x\n", i, k);
			}
		}
	}
	else
	{
		ErrorCode = -ENODEV;
	}
	return ErrorCode;
}

// vim:ts=4
