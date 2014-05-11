#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/delay.h>

#include <linux/workqueue.h>
#include <linux/input.h>

#include <linux/gpio.h>
#include <linux/stm/gpio.h>
#include "asc.h"

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17)
#include <linux/stm/pio.h>
#else
#include <linux/stpio.h>
#endif

#define LED1 stm_gpio(14, 1)
#define LED2 stm_gpio(14, 2)

//#define KEY_L_	stm_gpio(10, 2)
//#define KEY_R_	stm_gpio(10, 3)

#define KEY_R__	stm_gpio(6, 6)
#define KEY_L__	stm_gpio(6, 7)
#define KEY_PWR		stm_gpio(6, 5)

static char *button_driver_name = "SagemCom88 frontpanel buttons";
static struct input_dev *button_dev;
static struct workqueue_struct *wq;
static int bad_polling = 1;

static const unsigned char rom[256] =
{
        0x2e,//0x00, icon play
        0x8f,//0x01, icon stop
        0xe4,//0x02, icon pause
        0xdd,//0x03, icon ff
        0xdc,//0x04, icon rewind
        0x10,//0x05, 
        0x10,//0x06, 
        0x10,//0x07, 
        0x10,//0x08, 
        0x10,//0x09, 
        0x10,//0x0a, 
        0x10,//0x0b, 
        0x10,//0x0c, 
        0x10,//0x0d, 
        0x10,//0x0e, 
        0x10,//0x0f, 

        0x10,//0x10,,reserved  
        0x11,//0x11,,reserved   
        0x12,//0x12,,reserved  
        0x13,//0x13,,reserved  
        0x14,//0x14,,reserved  
        0x15,//0x15,,reserved  
        0x16,//0x16,,reserved  
        0x17,//0x17,,reserved  
        0x18,//0x18,,reserved  
        0x19,//0x19,,reserved  
        0x1a,//0x1a,,reserved   
        0x1b,//0x1b,,reserved  
        0x1c,//0x1c,,reservedr 
        0x10,//0x1d,,reserved  
        0x10,//0x1e,,reserved  
        0x10,//0x1f, , reserved

        0x00,//0x20, <space> ok
        0x21,//0x21,!
        0x22,//0x22,"
        0x23,//0x23,#
        0x24,//0x24,$
        0x25,//0x25,%
        0x26,//0x26,&
        0x27,//0x27,'
        0x28,//0x28,(
        0x29,//0x29,)
        0x2a,//0x2a,*
        0x2b,//0x2b,+
        0x2c,//0x2c,,
        0x40,//0x2d,-	ok
        0x2e,//0x2e,.
        0x2f,//0x2f, /

        0x3f,//0x30,0    ok
        0x06,//0x31,1    ok
        0x5b,//0x32,2    ok
        0x4f,//0x33,3    ok
        0x66,//0x34,4    ok
        0x6d,//0x35,5    ok
        0x7d,//0x36,6    ok
        0x07,//0x37,7    ok
        0x7f,//0x38,8    ok
        0x6f,//0x39,9    ok
        0x3a,//0x3a,:    
        0x3b,//0x3b,;    
        0x3c,//0x3c,<    
        0x3d,//0x3d,=    
        0x3e,//0x3e,>    
        0x3f,//0x3f, ?  

        0x40,//0x40,@ 
        119,//0x41,A ok
        0x7f,//0x42,B ok
        57,//0x43,C ok
        0x3f,//0x44,D ok
        121,//0x45,E ok
        113,//0x46,F ok
        0x7d,//0x47,G ok
        0x76,//0x48,H ok
        0x06,//0x49,I ok
        14,//0x4a,J ok
        0x76,//0x4b,K ok
        56,//0x4c,L ok
        55,//0x4d,M ok
        55,//0x4e,N ok
        0x3f,//0x4f,O ok

        115,//0x50,P  ok
        0x3f,//0x51,Q  ok
        119,//0x52,R  ok
        109,//0x53,S  ok
        49,//0x54,T  ok
        62,//0x55,U  ok
        62,//0x56,V  ok
        62,//0x57,W  ok
        0x76,//0x58,X  ok
        102,//0x59,Y  ok
        0x5b,//0x5a,Z  ok
        0x30,//0x5b [  ok
        0x5c,//0x5c,  l
        0x06,//0x5d,]  ok
        0x5e,//0x5e,^  
        0x5f,//0x5f,  

        0x60,//0x60,`  
        119,//0x61,a  ok
        124,//0x62,b  ok
        88,//0x63,c  ok
        94,//0x64,d  ok
        121,//0x65,e  ok
        113,//0x66,f  ok
        0x6f,//0x67,g  ok
        116,//0x68,h  ok
        0x04,//0x69,i  ok
        14,//0x6a,j  ok
        120,//0x6b,k  ok
        56,//0x6c,l  ok
        0x54,//0x6d,m  ok
        0x54,//0x6e,n  ok
        92,//0x6f,o  ok

        115,//0x70,p  ok
        103,//0x71,q  ok
        0x50,//0x72,r  ok
        109,//0x73,s  ok
        120,//0x74,t  ok
        0x1c,//0x75,u  ok
        0x1c,//0x76,v  ok
        0x1c,//0x77,w  ok
        0x76,//0x78,x  ok
        110,//0x79,y  ok
        0x5b,//0x7a,z  ok
        0x7b,//0x7b,{  
        0x7c,//0x7c,  
        0x7d,//0x7d,}  
        0x7e,//0x7e,~  
        0x7f,//0x7f, <DEL>--> all light on

        0x84,//0x80,a -umulaut  
        0x94,//0x81,o -umulaut  
        0x81,//0x82,u -umulaut  
        0x8e,//0x83,A -umulaut  
        0x99,//0x84,O -umulaut  
        0x9a,//0x85,U -umulaut  
        0xb1,//0x86,��- estset  
        0x10,//0x87,reserved  
        0x10,//0x88,reserved   
        0x10,//0x89,reserved  
        0x10,//0x8a,reserved  
        0x10,//0x8b,reserved  
        0x10,//0x8c,reserved  
        0x10,//0x8d,reserved  
        0x10,//0x8e,reserved  
        0x10,//0x8f,  reserved 

        0x10,//0x90,reserved 
        0x10,//0x91,reserved  
        0x10,//0x92,reserved 
        0x10,//0x93,reserved 
        0x10,//0x94,reserved 
        0x10,//0x95,reserved 
        0x10,//0x96,reserved 
        0x10,//0x97,reserved 
        0x10,//0x98,reserved 
        0x10,//0x99,reserved 
        0x10,//0x9a,reserved  
        0x10,//0x9b,reserved 
        0x10,//0x9c,reservedr
        0x10,//0x9d,reserved 
        0x10,//0x9e,reserved 
        0x10,//0x9f, reserved

        0x10,//0xa0,reserved  
        0x10,//0xa1,reserved   
        0x10,//0xa2,reserved   
        0x10,//0xa3,reserved   
        0x10,//0xa4,reserved   
        0x10,//0xa5,reserved   
        0x10,//0xa6,reserved   
        0x10,//0xa7,reserved   
        0x10,//0xa8,reserved   
        0x10,//0xa9,reserved   
        0x10,//0xaa,reserved   
        0x10,//0xab,reserved   
        0x10,//0xac,eserved   
        0x10,//0xad,reserved   
        0x10,//0xae,reserved   
        0x10,//0xaf, reserved 

        0x10,//0xb0, reserved 
        0x10,//0xb1, reserved 
        0x10,//0xb2, reserved 
        0x10,//0xb3, reserved 
        0x10,//0xb4, reserved 
        0x10,//0xb5, reserved 
        0x10,//0xb6, reserved 
        0x10,//0xb7, reserved 
        0x10,//0xb8, reserved 
        0x10,//0xb9, reserved 
        0x10,//0xba, reserved 
        0x10,//0xbb, reserved 
        0x10,//0xbc,reserved 
        0x10,//0xbd, reserved 
        0x10,//0xbe, reserved 
        0x10,//0xbf, reserved

        0x10,//0xc0, reserved  
        0x10,//0xc1, reserved  
        0x10,//0xc2, reserved  
        0x10,//0xc3, reserved  
        0x10,//0xc4, reserved  
        0x10,//0xc5, reserved  
        0x10,//0xc6, reserved  
        0x10,//0xc7, reserved  
        0x10,//0xc8, reserved  
        0x10,//0xc9, reserved  
        0x10,//0xca, reserved  
        0x10,//0xcb, reserved  
        0x10,//0xcc, reserved 
        0x10,//0xcd, reserved  
        0x10,//0xce, reserved  
        0x10,//0xcf, reserved 

        0x10,//0xd0, reserved  
        0x10,//0xd1, reserved  
        0x10,//0xd2, reserved  
        0x10,//0xd3, reserved  
        0x10,//0xd4, reserved  
        0x10,//0xd5, reserved  
        0x10,//0xd6, reserved  
        0x10,//0xd7, reserved  
        0x10,//0xd8, reserved  
        0x10,//0xd9, reserved  
        0x10,//0xda, reserved  
        0x10,//0xdb, reserved  
        0x10,//0xdc,reserved  
        0x10,//0xdd, reserved  
        0x10,//0xde, reserved  
        0x10,//0xdf, reserved 

        0x10,//0xe0, reserved  
        0x10,//0xe1, reserved  
        0x10,//0xe2, reserved  
        0x10,//0xe3, reserved  
        0x10,//0xe4, reserved  
        0x10,//0xe5, reserved  
        0x10,//0xe6, reserved  
        0x10,//0xe7, reserved  
        0x10,//0xe8, reserved  
        0x10,//0xe9, reserved  
        0x10,//0xea, reserved  
        0x10,//0xeb, reserved  
        0x10,//0xec,reserved  
        0x10,//0xed, reserved  
        0x10,//0xee, reserved  
        0x10,//0xef, reserved 

	//kolejne segmenty wyswietlacza
        0x01,//0xf0,  reserved
        0x02,//0xf1,  reserved
        0x04,//0xf2,  reserved
        0x08,//0xf3,  reserved
        0x10,//0xf4,  reserved
        0x20,//0xf5,  reserved
        0x40,//0xf6,  reserved
        0x80,//0xf7,  reserved
        0x10,//0xf8,  reserved
        0x10,//0xf9,  reserved
        0x10,//0xfa,  reserved
        0x10,//0xfb,  reserved
        0x10,//0xfc, reserved
        0x10,//0xfd,  reserved
        0x10,//0xfe,  reserved
        0x10,//0xff, reservedr
};

#define VFD_MAJOR	147

struct vfd_ioctl_data {
    unsigned char address;
    unsigned char data[64];
    unsigned char length;
};

struct vfd_driver {
  struct semaphore      sem;
  int opencount;
};

spinlock_t mr_lock = SPIN_LOCK_UNLOCKED;

static struct vfd_driver vfd;
static int debug  = 0;
static jasnosc = 1;

#define DBG(fmt, args...) if ( debug ) printk(KERN_INFO "[vfd] :: " fmt "\n", ## args )
#define ERR(fmt, args...) printk(KERN_ERR "[vfd] :: " fmt "\n", ## args )



#define BUTTON_FRONT

#ifdef BUTTON_FRONT

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
void button_bad_polling(void)
#else
void button_bad_polling(struct work_struct *ignored)
#endif
{
unsigned int  key_front;
	
	while(bad_polling == 1)
	{
	msleep(100);

//DBG("-%d\n",gpio_get_value(KEY_LEFT));
		
	key_front=0;
	if(gpio_get_value(KEY_L__)==0) key_front=KEY_LEFT;
	//if(gpio_get_value(KEY_L_)==0) key_front=KEY_LEFT;
	if(gpio_get_value(KEY_PWR)==0) key_front=KEY_POWER;
	if(gpio_get_value(KEY_R__)==0) key_front=KEY_RIGHT;
	//if(gpio_get_value(KEY_R_)==0) key_front=KEY_RIGHT;
		
		if(key_front>0)
			{
			input_report_key(button_dev, key_front, 1);
			input_sync(button_dev);
			DBG("Key:%d 0x%x\n",key_front,key_front);
			msleep(250);
			input_report_key(button_dev, key_front, 0);
			input_sync(button_dev);			
			}
	
	}//while
	DBG("[BTN] button_bad_polling END\n");
	bad_polling = 2;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static DECLARE_WORK(button_obj, button_bad_polling, NULL); 
#else
static DECLARE_WORK(button_obj, button_bad_polling); 
#endif

static int button_input_open(struct input_dev *dev)
{
	DBG("[BTN] button_input_open\n");

	wq = create_workqueue("button");                   
	if(queue_work(wq, &button_obj))
	{                    
		DBG("[BTN] queue_work successful ...\n");
	}
	else
	{
		DBG("[BTN] queue_work not successful, exiting ...\n");
		return 1;
	}
	return 0;
}

static void button_input_close(struct input_dev *dev)
{
	DBG("[BTN] button_input_close\n");
	bad_polling = 0;
	while (bad_polling != 2)
		msleep(1);
	bad_polling = 1;

	if (wq)
	{
		DBG("[BTN] workqueue destroy start\n");
		destroy_workqueue(wq);                           
		DBG("[BTN] workqueue destroyed\n");
	}
}

#endif

unsigned int ASCXBaseAddress = ASC2BaseAddress;

void serial_init (void)
{
    *(unsigned int*)(ASCXBaseAddress + ASC_INT_EN)   = 0x00000000; // TODO: Why do we set here the INT_EN again ???
    *(unsigned int*)(ASCXBaseAddress + ASC_CTRL)     = 0x00001589;
    *(unsigned int*)(ASCXBaseAddress + ASC_TIMEOUT)  = 0x00000010;
    *(unsigned int*)(ASCXBaseAddress + ASC_BAUDRATE) = 0x000000c9;
    *(unsigned int*)(ASCXBaseAddress + ASC_TX_RST)   = 0;
    *(unsigned int*)(ASCXBaseAddress + ASC_RX_RST)   = 0;
}

int serial_putc (char Data)
{
    char                  *ASC_3_TX_BUFF = (char*)(ASCXBaseAddress + ASC_TX_BUFF);
    unsigned int          *ASC_3_INT_STA = (unsigned int*)(ASCXBaseAddress + ASC_INT_STA);
    unsigned long         Counter = 200000;

    while (((*ASC_3_INT_STA & ASC_INT_STA_THE) == 0) && --Counter)
    {
        udelay(0);
    }

    if (Counter == 0)
    {
        printk("Error writing char (%c) \n", Data);
    }

    *ASC_3_TX_BUFF = Data;
    return 1;
}

int WriteChars(unsigned char* aData, int aSize)
{
	int iii;

	for(iii =0 ; iii < aSize ; iii++)
	{	udelay(100);
		serial_putc((aData[iii]));
	}
/*	printf("WriteChars :");
	for(iii=0; iii< aSize; iii++)
	{
		printf("[0x%x]",aData[iii]);

	}
//
	printf("\n");*/
	return 0;
	
}


int ESI88_WriteFront(unsigned char* data, unsigned char len )
{
unsigned char wdata[8];
int i = 0;
unsigned char crc=0xff;

  if(len<4) return 0;
  wdata[0] = 0x81;crc=crc^wdata[0];
  wdata[1] = rom[data[0]];crc=crc^wdata[1];
  wdata[2] = rom[data[1]];crc=crc^wdata[2];
  wdata[3] = rom[data[2]];crc=crc^wdata[3];
  wdata[4] = rom[data[3]];crc=crc^wdata[4];

  if(len>4)
  {
	  if(data[4]=='0') {wdata[5] = 0x00;crc=crc^wdata[5];}//dwie kropki
	  else
	  if(data[4]=='1') {wdata[5] = 0x01;crc=crc^wdata[5];}//dwie kropki
	  else
	  if(data[4]=='2') {wdata[5] = 0x02;crc=crc^wdata[5];}//dwie kropki
	  else
	  if(data[4]=='3') {wdata[5] = 0x03;crc=crc^wdata[5];}//dwie kropki
	  else
		  {wdata[5] = 0x00;crc=crc^wdata[5];}//dwie kropki
  }
	else
		  {wdata[5] = 0x00;crc=crc^wdata[5];}//dwie kropki

/*
  if(len>5)
  {
	if(data[5]=='0') {wdata[6] = 0x00;crc=crc^wdata[6];}//jasnosc
	  else
	if(data[5]=='1') {wdata[6] = 0x01;crc=crc^wdata[6];}//jasnosc
	  else
		{wdata[6] = 0x00;crc=crc^wdata[6];}//jasnosc
  }
	else
		{wdata[6] = 0x01;crc=crc^wdata[6];}//jasnosc 1=jasniejszy
*/
	if (jasnosc==0) 
		{wdata[6] = 0x00;crc=crc^wdata[6];}
	else
		{wdata[6] = 0x01;crc=crc^wdata[6];}

wdata[7] = crc;
  WriteChars(wdata, 8);
  return 0;
}

static void ESI88_set_icon(unsigned char *kbuf, unsigned char len)
{
        spin_lock(&mr_lock);

        switch(kbuf[0])
        {
        case 1://czer
            if(kbuf[4]==1) gpio_set_value(LED1, 1); else gpio_set_value(LED1, 0);
            break;
        case 2://ziel
            if(kbuf[4]==1) gpio_set_value(LED2, 1); else gpio_set_value(LED2, 0);
            break;
        default:
            ERR("icon unknown %d", kbuf[0]);
            break;
        }
            
        spin_unlock(&mr_lock);
}


static int ESI88_ShowBuf(unsigned char *kbuf, unsigned char len)
{
return 0;
}

static void ESI88_setup(void)
{
}

static void ESI88_set_brightness(int level )
{
	spin_lock(&mr_lock);


	if((level<0)||(level==0)) jasnosc=0;
	if(level>0) jasnosc=1;
	spin_unlock(&mr_lock);	
}

static void ESI88_set_lights(int onoff )
{
	spin_lock(&mr_lock);
	
//	if (onoff==1) 
//	else
	
	spin_unlock(&mr_lock);	
}

//-------------------------------------------------------------------------------------------
// VFD
//-------------------------------------------------------------------------------------------

#define VFDIOC_DCRAMWRITE			0xc0425a00
#define VFDIOC_BRIGHTNESS			0xc0425a03
#define VFDIOC_DISPLAYWRITEONOFF		0xc0425a05
#define VFDIOC_DRIVERINIT			0xc0425a08
#define VFDIOC_ICONDISPLAYONOFF			0xc0425a0a

static int vfd_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg ) {
  struct vfd_ioctl_data vfddata;

  switch( cmd ) {
  case VFDIOC_DCRAMWRITE:
    copy_from_user( &vfddata, (void*)arg, sizeof( struct vfd_ioctl_data ) );
    return ESI88_WriteFront(vfddata.data,vfddata.length);
    break;
  case VFDIOC_BRIGHTNESS:
    copy_from_user( &vfddata, (void*)arg, sizeof( struct vfd_ioctl_data ) );
    ESI88_set_brightness(vfddata.address );
    break;
  case VFDIOC_DISPLAYWRITEONOFF:
//    copy_from_user( &vfddata, (void*)arg, sizeof( struct vfd_ioctl_data ) );
//    ESI88_set_lights(vfddata.address );
    break;
  case VFDIOC_ICONDISPLAYONOFF:
    copy_from_user(&vfddata, (void*)arg, sizeof( struct vfd_ioctl_data ) );
    ESI88_set_icon(vfddata.data,vfddata.length);
	break;
  case VFDIOC_DRIVERINIT:
//	    ESI88_setup();
    break;
  default:
    ERR("[vfd] unknown ioctl %08x", cmd );
    break;
  }
  return 0;

}

static ssize_t vfd_write( struct file *filp, const char *buf, size_t len, loff_t *off ) {
  unsigned char  kbuf[8];
  size_t         wlen;

  DBG("vfd_write");

  DBG("write : len = %d (%d), off = %d.\n", len, 4, (int)*off);

  if ( len == 0 ) return len;

  if(len>6) len=6;	//z1 z2 z3 z4 kropki jasnosc

  copy_from_user(kbuf, buf, len); 
  
  wlen = len;
  if ( kbuf[len-1] == '\n' ) { kbuf[len-1] = '\0'; wlen--; }

  DBG("write : len = %d, wlen = %d, kbuf = '%s'.\n", len, wlen, kbuf);

  ESI88_WriteFront(kbuf,wlen);

  return len;
}

static ssize_t vfd_read(struct file *filp, char *buf, size_t len, loff_t *off ) {
  return len;
}

static int vfd_open( struct inode *inode, struct file *file ) {

  DBG("vfd_open");

  if ( down_interruptible( &(vfd.sem) ) ) {
        DBG("interrupted while waiting for sema.");
    return -ERESTARTSYS;
  }

  if ( vfd.opencount > 0 ) {
        DBG("device already opened.");
    up( &(vfd.sem) );
    return -EUSERS;
  }

  vfd.opencount++;
  up( &(vfd.sem) );

  return 0;
}

static int vfd_close( struct inode *inode, struct file *file ) {
  DBG("vfd_close");
  vfd.opencount = 0;
  return 0;
}

static struct file_operations vfd_fops = {
  .owner   = THIS_MODULE,
  .ioctl   = vfd_ioctl,
  .write   = vfd_write,
  .read    = vfd_read,
  .open    = vfd_open,
  .release = vfd_close
};

static void __exit led_module_exit(void) 
{
  unregister_chrdev( VFD_MAJOR, "vfd" );
#ifdef BUTTON_FRONT
  input_unregister_device(button_dev);
#endif
}

static int __init led_module_init( void ) {
int error;
	
	DBG("LED ESI88 init.");
	DBG("register character device %d.", VFD_MAJOR );
	if(register_chrdev( VFD_MAJOR, "vfd", &vfd_fops ) ) 
	{
	ERR("register major %d failed", VFD_MAJOR );
	goto led_init_fail;
	}

    // Address for FiFo enable/disable
    unsigned int         *ASC_X_CTRL       = (unsigned int*)(ASCXBaseAddress + ASC_CTRL);

    serial_init();

    //Enable the FIFO
    *ASC_X_CTRL = *ASC_X_CTRL | ASC_CTRL_FIFO_EN;

	sema_init( &(vfd.sem), 1 );
	vfd.opencount = 0;

gpio_request(LED1, "LED1");
if (LED1==NULL){ERR("Request LED1 failed. abort.");goto led_init_fail;}
gpio_direction_output(LED1, STM_GPIO_DIRECTION_OUT);
gpio_set_value(LED1, 0);//czerwona
  
gpio_request(LED2, "LED2");
if (LED2==NULL){ERR("Request LED2 failed. abort.");goto led_init_fail;}
gpio_direction_output(LED2, STM_GPIO_DIRECTION_OUT);
gpio_set_value(LED2, 1);//zielona

#ifdef BUTTON_FRONT

gpio_request(KEY_L__, "KEY_LEFT");
if (KEY_L__==NULL){ERR("Request KEY_LEFT failed. abort.");goto led_init_fail;}
//gpio_direction_output(KEY_LEFT, STM_GPIO_DIRECTION_IN);
gpio_direction_input(KEY_L__);

gpio_request(KEY_R__, "KEY_RIGHT");
if (KEY_R__==NULL){ERR("Request KEY_RIGHT failed. abort.");goto led_init_fail;}
//gpio_direction_output(KEY_RIGHT, STM_GPIO_DIRECTION_IN);
gpio_direction_input(KEY_R__);

gpio_request(KEY_PWR, "KEY_PWR");
if (KEY_PWR==NULL){ERR("Request KEY_PWR failed. abort.");goto led_init_fail;}
//gpio_direction_output(KEY_PWR, STM_GPIO_DIRECTION_IN);
gpio_direction_input(KEY_PWR);

//gpio_request(KEY_L_, "KEY_LEFT_");
//if (KEY_L_==NULL){ERR("Request KEY_LEFT_ failed. abort.");goto led_init_fail;}
//gpio_direction_output(KEY_LEFT_, STM_GPIO_DIRECTION_IN);
//gpio_direction_input(KEY_L_);

//gpio_request(KEY_R_, "KEY_RIGHT_");
//if (KEY_R_==NULL){ERR("Request KEY_RIGHT_ failed. abort.");goto led_init_fail;}
//gpio_direction_output(KEY_RIGHT_, STM_GPIO_DIRECTION_IN);
//gpio_direction_input(KEY_R_);

	button_dev = input_allocate_device();
	if (!button_dev){
		ERR("Error : input_allocate_device");
		goto led_init_fail;
	}
	button_dev->name = button_driver_name;
	button_dev->open = button_input_open;
	button_dev->close = button_input_close;
	
	set_bit(EV_KEY, button_dev->evbit);
	set_bit(KEY_RIGHT, button_dev->keybit); 
	set_bit(KEY_LEFT, button_dev->keybit); 
	set_bit(KEY_POWER, button_dev->keybit); 

	error = input_register_device(button_dev);
	if (error) {
		input_free_device(button_dev);
		ERR("Request input_register_device. abort.");
		goto led_init_fail;
	}
#endif

return 0;

  led_init_fail:
  led_module_exit();
  return -EIO;
}

module_init(led_module_init);
module_exit(led_module_exit);

MODULE_DESCRIPTION("SagemCom88 front_led driver");
MODULE_AUTHOR("Nemo");
MODULE_LICENSE("GPL");
