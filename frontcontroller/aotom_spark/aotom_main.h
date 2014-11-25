#ifndef __AOTOM_MAIN_H__
#define __AOTOM_MAIN_H__

#ifndef __KERNEL__
typedef unsigned char u8;

typedef unsigned short u16;

typedef unsigned int u32;
#endif

#define YWPANEL_MAX_LED_LENGTH        4
#define YWPANEL_MAX_VFD_LENGTH        8
#define YWPANEL_MAX_DVFD_LENGTH       16
#define LOG_OFF                       0
#define LOG_ON                        1
#define LED_RED                       0
#if defined(SPARK)
#define LASTLED                       2
#define LED_GREEN                     1
#elif defined(SPARK7162)
#define LASTLED                       1
#define LED_SPINNER                   1
/* Uncomment next line to enable lower case letters on Spark 7162 (D)VFD */
//#define LOWER_CASE                   1
#endif

#define VFDDISPLAYCHARS               0xc0425a00
#define VFDBRIGHTNESS                 0xc0425a03
#define VFDPWRLED                     0xc0425a04 // unused in aotom, reserved and deprecated for Fortis HDBOX
#define VFDDISPLAYWRITEONOFF          0xc0425a05
#define VFDDRIVERINIT                 0xc0425a08
#define VFDICONDISPLAYONOFF           0xc0425a0a

#define VFDGETBLUEKEY                 0xc0425af1
#define VFDSETBLUEKEY                 0xc0425af2
#define VFDGETSTBYKEY                 0xc0425af3
#define VFDSETSTBYKEY                 0xc0425af4
#define VFDPOWEROFF                   0xc0425af5
#define VFDSETPOWERONTIME             0xc0425af6
#define VFDGETVERSION                 0xc0425af7
#define VFDGETSTARTUPSTATE            0xc0425af8
#define VFDGETWAKEUPMODE              0xc0425af9
#define VFDGETTIME                    0xc0425afa
#define VFDSETTIME                    0xc0425afb
#define VFDSTANDBY                    0xc0425afc
#define VFDSETTIME2                   0xc0425afd // seife, set 'complete' time...
#define VFDSETLED                     0xc0425afe
#define VFDSETMODE                    0xc0425aff
#define VFDDISPLAYCLR                 0xc0425b00
#define VFDGETLOOPSTATE               0xc0425b01
#define VFDSETLOOPSTATE               0xc0425b02
#define VFDGETWAKEUPTIME              0xc0425b03 // added by Audioniek

#define INVALID_KEY                   -1
#define VFD_MAJOR                     147

#define	I2C_BUS_NUM                   1
#define	I2C_BUS_ADD                   (0x50>>1)  //this is important not 0x50

#define YWPANEL_FP_INFO_MAX_LENGTH    10
#define YWPANEL_FP_DATA_MAX_LENGTH    38

static const char Revision[] = "Revision: 0.8";
typedef unsigned int YWOS_ClockMsec;

typedef struct YWPANEL_I2CData_s
{
	u8 writeBuffLen;
	u8 writeBuff[YWPANEL_FP_DATA_MAX_LENGTH];
	u8 readBuff[YWPANEL_FP_INFO_MAX_LENGTH];

} YWPANEL_I2CData_t;

struct set_brightness_s
{
	int level;
};

struct set_icon_s
{
	int icon_nr;
	int on;
};

struct set_led_s
{
	int led_nr;
	int on;
};

struct set_light_s
{
	int onoff;
};

/* time must be given as follows:
 * time[0] & time[1] = mjd ???
 * time[2] = hour
 * time[3] = min
 * time[4] = sec
 */
struct set_standby_s
{
	char time[5];
};

struct set_time_s
{
	char time[5];
};

struct set_key_s
{
	int key_nr;
	u32 key;
};

/* This changes the mode temporarily (for one IOCTL)
 * to the desired mode. currently the "normal" mode
 * is the compatible VFD mode
 */
struct set_mode_s
{
	int compat; /* 0 = compatibility mode to VFD driver; 1 = nuvoton mode */
};

struct aotom_ioctl_data
{
	union
	{
		struct set_icon_s icon;
		struct set_brightness_s brightness;
		struct set_led_s led;
		struct set_light_s light;
		struct set_mode_s mode;
		struct set_standby_s standby;
		struct set_time_s time;
		struct set_key_s key;
	} u;
};

struct vfd_ioctl_data
{
	unsigned char start_address;
	unsigned char data[64];
	unsigned char length;
};

// Icon names for Spark7162
enum
{
/*----------------------------------11G-------------------------------------*/
	VICON_FIRST = 11 * 16 + 1, //Icon 1
	ICON_FIRST = 1, //Icon 1
	ICON_REWIND = ICON_FIRST,
	ICON_PLAY_STEPBACK,
	ICON_PLAY_LOG,
	ICON_PLAY_STEP,
	ICON_FASTFORWARD,
	ICON_PAUSE,
	ICON_REC1,
	ICON_MUTE,
	ICON_REPEAT,
	ICON_DOLBY,
	ICON_CA,
	ICON_CI,
	ICON_USB,
	ICON_DOUBLESCREEN,
	ICON_REC2,
/*----------------------------------12G-------------------------------------*/
	ICON_HDD_A8, //Icon 16
	ICON_HDD_A7,
	ICON_HDD_A6,
	ICON_HDD_A5,
	ICON_HDD_A4,
	ICON_HDD_A3,
	ICON_HDD_FULL,
	ICON_HDD_A2,
	ICON_HDD_A1,
	ICON_MP3,
	ICON_AC3,
	ICON_TVMODE,
	ICON_AUDIO,
	ICON_ALERT,
	ICON_HDD_GRID,
/*----------------------------------13G-------------------------------------*/
	ICON_CLOCK_PM, //Icon 31
	ICON_CLOCK_AM,
	ICON_TIMER,
	ICON_TIME_COLON,
	ICON_DOT2,
	ICON_STANDBY,
	ICON_TER,
	ICON_DISK_S3,
	ICON_DISK_S2,
	ICON_DISK_S1,
	ICON_DISK_CIRCLE,
	ICON_SAT,
	ICON_TIMESHIFT,
	ICON_DOT1,
	ICON_CABLE,
	VICON_LAST = 13 * 16 + 15,
	ICON_LAST = ICON_CABLE,
/*----------------------------------End-------------------------------------*/
	ICON_ALL, //Table count = 46
	ICON_SPINNER, // 47
};

typedef enum FPMode_e
{
	FPWRITEMODE,
	FPREADMODE
} FPMode_T;

typedef enum SegNum_e
{
	SEGNUM1 = 0,
	SEGNUM2
} SegNum_T;

typedef struct SegAddrVal_s
{
	u8 Segaddr1;
	u8 Segaddr2;
	u8 CurrValue1;
	u8 CurrValue2;
} SegAddrVal_T;

typedef enum YWPANEL_DataType_e
{
	YWPANEL_DATATYPE_LBD = 0x01,
	YWPANEL_DATATYPE_LCD,
	YWPANEL_DATATYPE_LED,
	YWPANEL_DATATYPE_VFD,
	YWPANEL_DATATYPE_DVFD,
	YWPANEL_DATATYPE_SCANKEY,
	YWPANEL_DATATYPE_IRKEY,

	YWPANEL_DATATYPE_GETVERSION,

	YWPANEL_DATATYPE_GETCPUSTATE,
	YWPANEL_DATATYPE_SETCPUSTATE,
	YWPANEL_DATATYPE_GETFPSTATE,
	YWPANEL_DATATYPE_SETFPSTATE,

	YWPANEL_DATATYPE_GETSTBYKEY1,
	YWPANEL_DATATYPE_GETSTBYKEY2,
	YWPANEL_DATATYPE_GETSTBYKEY3,
	YWPANEL_DATATYPE_GETSTBYKEY4,
	YWPANEL_DATATYPE_GETSTBYKEY5,
	YWPANEL_DATATYPE_SETSTBYKEY1,
	YWPANEL_DATATYPE_SETSTBYKEY2,
	YWPANEL_DATATYPE_SETSTBYKEY3,
	YWPANEL_DATATYPE_SETSTBYKEY4,
	YWPANEL_DATATYPE_SETSTBYKEY5,
	YWPANEL_DATATYPE_GETBLUEKEY1,
	YWPANEL_DATATYPE_GETBLUEKEY2,
	YWPANEL_DATATYPE_GETBLUEKEY3,
	YWPANEL_DATATYPE_GETBLUEKEY4,
	YWPANEL_DATATYPE_GETBLUEKEY5,
	YWPANEL_DATATYPE_SETBLUEKEY1,
	YWPANEL_DATATYPE_SETBLUEKEY2,
	YWPANEL_DATATYPE_SETBLUEKEY3,
	YWPANEL_DATATYPE_SETBLUEKEY4,
	YWPANEL_DATATYPE_SETBLUEKEY5,

	YWPANEL_DATATYPE_GETIRCODE,
	YWPANEL_DATATYPE_SETIRCODE,

	YWPANEL_DATATYPE_GETENCRYPTMODE,
	YWPANEL_DATATYPE_SETENCRYPTMODE,
	YWPANEL_DATATYPE_GETENCRYPTKEY,
	YWPANEL_DATATYPE_SETENCRYPTKEY,

	YWPANEL_DATATYPE_GETVERIFYSTATE,
	YWPANEL_DATATYPE_SETVERIFYSTATE,

	YWPANEL_DATATYPE_GETTIME,
	YWPANEL_DATATYPE_SETTIME,
	YWPANEL_DATATYPE_CONTROLTIMER,

	YWPANEL_DATATYPE_SETPOWERONTIME,
	YWPANEL_DATATYPE_GETPOWERONTIME,

	YWPANEL_DATATYPE_GETFPSTANDBYSTATE,
	YWPANEL_DATATYPE_SETFPSTANDBYSTATE,

	YWPANEL_DATATYPE_GETPOWERONSTATE,
	YWPANEL_DATATYPE_SETPOWERONSTATE,
	YWPANEL_DATATYPE_GETSTARTUPSTATE,
	YWPANEL_DATATYPE_SETSTARTUPSTATE,
	YWPANEL_DATATYPE_GETLOOPSTATE,
	YWPANEL_DATATYPE_SETLOOPSTATE,

	YWPANEL_DATATYPE_NUM
} YWPANEL_DataType_t;

typedef struct YWPANEL_LBDData_s
{
	u8 value;
} YWPANEL_LBDData_t;

typedef struct YWPANEL_LEDData_s
{
	u8 led1;
	u8 led2;
	u8 led3;
	u8 led4;
} YWPANEL_LEDData_t;

typedef enum YWPANEL_VFDDataType_e
{
	YWPANEL_VFD_SETTING = 1,
	YWPANEL_VFD_DISPLAY,
	YWPANEL_VFD_READKEY,
	YWPANEL_VFD_DISPLAYSTRING
} YWPANEL_VFDDataType_t;

typedef struct YWPANEL_VFDData_s
{
	YWPANEL_VFDDataType_t type; /* 1- setting  2- display  3- readscankey  4- displaystring*/
	u8 setValue;         //if type == YWPANEL_VFD_SETTING
	u8 address[16];      //if type == YWPANEL_VFD_DISPLAY
	u8 DisplayValue[16]; //if type == YWPANEL_VFD_DISPLAYSTRING
	u8 key;              //if type == YWPANEL_VFD_READKEY

} YWPANEL_VFDData_t;

typedef enum YWPANEL_DVFDDataType_e
{
	YWPANEL_DVFD_SETTING = 1,
	YWPANEL_DVFD_DISPLAY,
	YWPANEL_DVFD_DISPLAYSTRING,
	YWPANEL_DVFD_DISPLAYSYNC,
	YWPANEL_DVFD_SETTIMEMODE,
	YWPANEL_DVFD_GETTIMEMODE
}YWPANEL_DVFDDataType_t;

typedef struct YWPANEL_DVFDData_s
{
	YWPANEL_DVFDDataType_t type;
	u8 setValue;
	u8 ulen;
	u8 address[16];
	u8 DisplayValue[16][5];
} YWPANEL_DVFDData_t;

typedef struct YWPANEL_ScanKey_s
{
	u32 keyValue;
} YWPANEL_ScanKey_t;

typedef struct YWPANEL_StandByKey_s
{
	u32 key;
} YWPANEL_StandByKey_t;

typedef enum YWPANEL_VERIFYSTATE_e
{
	YWPANEL_VERIFYSTATE_NONE = 0,
	YWPANEL_VERIFYSTATE_CRC16,
	YWPANEL_VERIFYSTATE_CRC32,
	YWPANEL_VERIFYSTATE_CHECKSUM
} YWPANEL_VERIFYSTATE_T;

typedef struct YWPANEL_VerifyState_s
{
	YWPANEL_VERIFYSTATE_T state;
} YWPANEL_VerifyState_t;

typedef struct YWPANEL_Time_s
{
	u32 second;
} YWPANEL_Time_t;

typedef struct YWPANEL_ControlTimer_s
{
	int startFlag; // 0 - stop  1 - start
} YWPANEL_ControlTimer_t;

typedef struct YWPANEL_FpStandbyState_s
{
	int On; // 0 - off  1 - on
} YWPANEL_FpStandbyState_t;

typedef struct YWPANEL_BlueKey_s
{
	u32 key;
} YWPANEL_BlueKey_t;

typedef enum YWPANEL_FPSTATE_e
{
	YWPANEL_FPSTATE_UNKNOWN,
	YWPANEL_FPSTATE_STANDBYOFF = 1,
	YWPANEL_FPSTATE_STANDBYON
} YWPANEL_FPSTATE_t;

typedef enum YWPANEL_CPUSTATE_s
{
	YWPANEL_CPUSTATE_UNKNOWN,
	YWPANEL_CPUSTATE_RUNNING = 1,
	YWPANEL_CPUSTATE_STANDBY
} YWPANEL_CPUSTATE_t;

typedef struct YWPANEL_CpuState_s
{
	YWPANEL_CPUSTATE_t state;
} YWPANEL_CpuState_t;

typedef struct YWFP_FuncKey_s
{
	u8 key_index;
	u32 key_value;
} YWFP_FuncKey_t;

typedef enum YWFP_TYPE_s
{
	YWFP_UNKNOWN,
	YWFP_COMMON,
	YWFP_STAND_BY
} YWFP_TYPE_t;

typedef struct YWFP_INFO_s
{
	YWFP_TYPE_t fp_type;
} YWFP_INFO_t;

typedef enum YWPANEL_POWERONSTATE_e
{
	YWPANEL_POWERONSTATE_UNKNOWN,
	YWPANEL_POWERONSTATE_RUNNING = 1,
	YWPANEL_POWERONSTATE_CHECKPOWERBIT
} YWPANEL_POWERONSTATE_t;

typedef struct YWPANEL_PowerOnState_s
{
	YWPANEL_POWERONSTATE_t state;
} YWPANEL_PowerOnState_t;

typedef enum YWPANEL_STARTUPSTATE_e
{
	YWPANEL_STARTUPSTATE_UNKNOWN = 0,
	YWPANEL_STARTUPSTATE_ELECTRIFY = 1,  //power on
	YWPANEL_STARTUPSTATE_STANDBY,        //from standby
	YWPANEL_STARTUPSTATE_TIMER           //woken by timer
} YWPANEL_STARTUPSTATE_t;

typedef struct YWPANEL_StartUpState_s
{
	YWPANEL_STARTUPSTATE_t state;
} YWPANEL_StartUpState_t;

typedef enum YWPANEL_LOOPSTATE_e
{
	YWPANEL_LOOPSTATE_UNKNOWN = 0,
	YWPANEL_LOOPSTATE_LOOPOFF = 1,
	YWPANEL_LOOPSTATE_LOOPON
} YWPANEL_LOOPSTATE_t;

typedef struct YWPANEL_LoopState_s
{
	YWPANEL_LOOPSTATE_t state;
} YWPANEL_LoopState_t;

typedef enum YWPANEL_LBDType_e
{ // LED masks
	YWPANEL_LBD_TYPE_POWER  = (1 << 0),
	YWPANEL_LBD_TYPE_SIGNAL = (1 << 1),
	YWPANEL_LBD_TYPE_MAIL   = (1 << 2),
	YWPANEL_LBD_TYPE_AUDIO  = (1 << 3)
} YWPANEL_LBDType_t;

typedef enum YWPANEL_FP_MCUTYPE_e
{
	YWPANEL_FP_MCUTYPE_UNKNOWN = 0,
	YWPANEL_FP_MCUTYPE_AVR_ATTING48, // AVR MCU
	YWPANEL_FP_MCUTYPE_AVR_ATTING88,
	YWPAN_FP_MCUTYPE_MAX
} YWPANEL_FP_MCUTYPE_t;

typedef enum YWPANEL_FP_DispType_e
{
	YWPANEL_FP_DISPTYPE_UNKNOWN = 0,
	YWPANEL_FP_DISPTYPE_VFD     = (1 << 0),
	YWPANEL_FP_DISPTYPE_LCD     = (1 << 1),
	YWPANEL_FP_DISPTYPE_DVFD    = (3),
	YWPANEL_FP_DISPTYPE_LED     = (1 << 2),
	YWPANEL_FP_DISPTYPE_LBD     = (1 << 3)
} YWPANEL_FP_DispType_t;

typedef struct YWPANEL_Version_s
{
	YWPANEL_FP_MCUTYPE_t CpuType;
	u8 DisplayInfo;
	u8 scankeyNum;
	u8 swMajorVersion;
	u8 swSubVersion;
} YWPANEL_Version_t;

typedef struct YWPANEL_FPData_s
{
	YWPANEL_DataType_t dataType;
	union
	{
		YWPANEL_Version_t         version;
		YWPANEL_LBDData_t         lbdData;
		YWPANEL_LEDData_t         ledData;
		YWPANEL_VFDData_t         vfdData;
		YWPANEL_DVFDData_t        dvfdData;
		YWPANEL_ScanKey_t         ScanKeyData;
		YWPANEL_CpuState_t        CpuState;
		YWPANEL_StandByKey_t      stbyKey;
		YWPANEL_VerifyState_t     verifyState;
		YWPANEL_Time_t            time;
		YWPANEL_ControlTimer_t    TimeState;
		YWPANEL_FpStandbyState_t  FpStandbyState;
		YWPANEL_BlueKey_t         BlueKey;
		YWPANEL_PowerOnState_t    PowerOnState;
		YWPANEL_StartUpState_t    StartUpState;
		YWPANEL_LoopState_t       LoopState;
	} data;
	int ack;
} YWPANEL_FPData_t;

int YWPANEL_FP_Init(void);
//extern int (*YWPANEL_VFD_Initialize)(void);
extern int (*YWPANEL_FP_Term)(void);
extern int (*YWPANEL_FP_ShowIcon)(int, int);
extern int (*YWPANEL_FP_ShowTime)(u8 hh, u8 mm);
extern int (*YWPANEL_FP_ShowTimeOff)(void);
extern int (*YWPANEL_FP_SetBrightness)(int);
extern u8 (*YWPANEL_FP_ScanKeyboard)(void);
extern int (*YWPANEL_FP_ShowString)(char *);
extern int (*YWPANEL_FP_ShowContent)(void);
extern int (*YWPANEL_FP_ShowContentOff)(void);

extern int YWPANEL_width;

//int YWPANEL_FP_GetRevision(char * version); //unused by aotom_main
//YWPANEL_FPSTATE_t YWPANEL_FP_GetFPStatus(void); //unused by aotom_main
//int YWPANEL_FP_SetFPStatus(YWPANEL_FPSTATE_t state); //unused by aotom_main
//YWPANEL_CPUSTATE_t YWPANEL_FP_GetCpuStatus(void); //unused by aotom_main
int YWPANEL_FP_SetCpuStatus(YWPANEL_CPUSTATE_t state);
int YWPANEL_FP_ControlTimer(int on);
//YWPANEL_POWERONSTATE_t YWPANEL_FP_GetPowerOnStatus(void); //unused by aotom_main
//int YWPANEL_FP_SetPowerOnStatus(YWPANEL_POWERONSTATE_t state); //unused by aotom_main
u32 YWPANEL_FP_GetTime(void);
int YWPANEL_FP_SetTime(u32 value);
int YWPANEL_FP_GetKey(int blue, int key_nr, u32 *k);
int YWPANEL_FP_SetKey(int blue, int key_nr, u32 k);
int YWPANEL_FP_GetStartUpState(YWPANEL_STARTUPSTATE_t *state);
int YWPANEL_FP_GetVersion(YWPANEL_Version_t *version);
int YWPANEL_FP_GetLoopState(YWPANEL_LOOPSTATE_t *state);
int YWPANEL_FP_SetLoopState(YWPANEL_LOOPSTATE_t state);
int YWPANEL_FP_SetPowerOnTime(u32 value);
u32 YWPANEL_FP_GetPowerOnTime(void);
int YWPANEL_FP_GetKeyValue(void);
int YWPANEL_FP_SetLed(int which, int on);
int utf8strlen(char *s, int len);
int utf8charlen(unsigned char c);

#endif /* __AOTOM_MAIN_H__ */

// vim:ts=4

