#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include "sii9135_utils.h"
#include "../../extdrv/i2c/i2c.h"
#include "../../extdrv/gpio_i2c_8b/gpio_i2c.h"
#include "mpi_sys.h"

#define SII9135_SLAVEADDR1       0x62
#define SII9135_SLAVEADDR2       0x6a

#define SII9135_SRST            0x05

#define SII9135_STATE           0x06
#define SII9135_PWR5V           0x08
#define SII9135_STATE_VSYNC     0x04
#define SII9135_STATE_SCDT      0x01

#define SII9135_DE_PIXL         0x4E
#define SII9135_DE_PIXH         0x4F
#define SII9135_DE_LINL         0x50
#define SII9135_DE_LINH         0x51

#define SII9135_RX_VID_STAT_ADDR  0x55             //R;Video Status Register.
#define SII9135_RX_BIT_INTRKL     0x04             //R;interlace detected.

#define SII9135_VID_MODE        0x4A

#define SII9135_INTR2           0x72
#define SII9135_INTR2_VSYNCDET  0x40    //vsync active edge recognized
#define SII9135_INTR2_SCDT      0x08    //sync detect changed
#define SII9135_INTR2_GOTCTS    0x04


#define SII9135_INTR2_GOTAUD    0x02    //received audio packed

#define SII9135_INTR3           0x73
#define SII9135_INTR3_NEWAUD    0x04    //NEW/change Audio InfoFrame detected.
#define SII9135_INTR3_NEWAVI    0x01    //NEW/change AVI InfoFrame detected.

#define SII9135_INTR5           0x7B
#define SII9135_VIDEOCHGMASK    0x1A
#define SII9135_RX_BIT_FSCHG    0x01    //Audio FS sample rate change.

#define SII9135_INTR6           0x7C
#define SII9135_UNPLUGINTR      0x01


#define SII9135_RX_AUDO_MUTE_ADDR   0x32                //audio out channel mute register.

#define SII9135_PD_SYS              0x3F
#define SII9135_PD_SYS_POWERON      0xED    /*enable all */
#define SII9135_PD_SYS_POWEROFF     0x01    /*except crystal input and audio pll */

#define SII9135_TCLK_FS             0x17        //audio sample 
#define SII9135_EXTRACTED_FS        0x0F

static int fd_i2c = -1;

#define AUDIO_SAMPLE_TABLE_NUMS 6
unsigned int audio_sample_table[AUDIO_SAMPLE_TABLE_NUMS][2] = {
    {0x00, 44100}, {0x02, 48000}, {0x0a, 96000}, 
    {0x03, 32000}, 
};

static unsigned char ddr1_reg_value[][2] = {
    {0x05, 0x90},  {0x07, 0x40},  {0x08, 0x05},  
    {0x09, 0x91},  {0x2e, 0x80},  {0x48, 0x05}, 
    {0x49, 0x83},  {0x5f, 0xc0},  {0xb5, 0x00}, 
    {0x81, 0x00},  {0x4a, 0xba},  {0x88, 0x88},
    {0x89, 0x16},  
};

static  unsigned char ddr2_reg_value[][2] = {
    {0x00, 0x00},  {0x02, 0x00},  {0x14, 0x20},
    {0x15, 0x00},  {0x16, 0x00},  {0x18, 0x54}, 
    {0x26, 0x00},  {0x27, 0xfd},  {0x29, 0x04},
    {0x2e, 0x00},  {0x32, 0x00},  {0x3f, 0xED},
    {0x3C, 0x00},
};

unsigned int reset_gpio[][2] = {
		// GPIO12_3
		{0x200f018c, 0x00},	//mux
		{0x20210400, 0x08},	//dir output
		{0x20210020, 0x00}, 	//data

		// GPIO11_0
		{0x200f0160, 0x00},	//mux
		{0x20200400, 0x01},	//dir
		{0x20200004, 0x00}, 	//data

		// GPIO11_1
		{0x200f0164, 0x00},	//mux
		{0x20200400, 0x02},	//dir
		{0x20200008, 0x00}, 	//data

		// GPIO7_1
		{0x200f00E4, 0x00},	//mux
		{0x201C0400, 0x02},	//dir
		{0x201C0008, 0x00}, 	//data
};


int sii9135_i2c_write(int i2c_simulate, int fd_i2c, unsigned char dev_addr, unsigned int reg_addr, unsigned int data)
{
	int ret = 0;
	if(fd_i2c < 0)
		return -1;
	if (i2c_simulate) {
		unsigned int value = ((dev_addr&0xff)<<24) | ((reg_addr&0xff)<<16) | (data&0xffff);
		ret = ioctl(fd_i2c, GPIO_I2C_WRITE, &value);
	} else {
		I2C_DATA_S i2c_data;
		i2c_data.dev_addr = dev_addr;
		i2c_data.reg_addr = reg_addr;
		i2c_data.addr_byte = 1;
		i2c_data.data = data;
		i2c_data.data_byte = 1;
		ret = ioctl(fd_i2c, I2C_CMD_WRITE, &i2c_data);
	}
	return ret;
}

int sii9135_i2c_read(int i2c_simulate, int fd_i2c, unsigned char dev_addr, unsigned int reg_addr)
{
	int ret;
	if(fd_i2c < 0)
			return -1;
	if (i2c_simulate) {
		unsigned int value = ((dev_addr&0xff)<<24) | ((reg_addr&0xff)<<16);
		ret = ioctl(fd_i2c, GPIO_I2C_READ, &value);
		return value&0xff;
	} else {
		I2C_DATA_S i2c_data;
		i2c_data.dev_addr = dev_addr;
		i2c_data.reg_addr = reg_addr;
		i2c_data.addr_byte = 1;
		i2c_data.data_byte = 1;
		i2c_data.data = 0;
		ret = ioctl(fd_i2c, I2C_CMD_READ, &i2c_data);
		if(ret)
		{
			return -1 ;
		}
		return i2c_data.data;
	}
}

void sii9135_reset_dev(int slot)
{
	int value;
	HI_MPI_SYS_SetReg(reset_gpio[slot*3][0], reset_gpio[slot*3][1]);
	HI_MPI_SYS_GetReg(reset_gpio[slot*3+1][0], &value);
	HI_MPI_SYS_SetReg(reset_gpio[slot*3+1][0], value|reset_gpio[slot*3+1][1]);
    //pull down rst
	HI_MPI_SYS_SetReg(reset_gpio[slot*3+2][0], 0x00);
    usleep(20000);
    //pull up rst
    HI_MPI_SYS_SetReg(reset_gpio[slot*3+2][0], 0xff);
}

int sii9135_init(int slot, int simulate, int fd_i2c)
{
    int i, size;
    printf("sii9135 slot=%d \n", slot);
	sii9135_reset_dev(slot);

    size = sizeof(ddr1_reg_value)/2;
    for (i=0; i<size; i++) {        
        sii9135_i2c_write(simulate, fd_i2c, SII9135_SLAVEADDR1, ddr1_reg_value[i][0], ddr1_reg_value[i][1]);
    }

    int value = 0;
	for (i=0; i<size; i++) {
		value = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR1, ddr1_reg_value[i][0]);
		printf("read reg:%x %x\n",ddr1_reg_value[i][0], value);
	}
    size = sizeof(ddr2_reg_value)/2;
    for (i=0; i<size; i++) {
        sii9135_i2c_write(simulate, fd_i2c, SII9135_SLAVEADDR2, ddr2_reg_value[i][0], ddr2_reg_value[i][1]);
    } 

	for (i=0; i<size; i++) {
		value = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR2, ddr2_reg_value[i][0]);
		printf("read reg:%x %x\n",ddr2_reg_value[i][0], value);
	}
    return 0;
}



int sii9135_check_cable_plug(int simulate, int fd_i2c, char *plugin)
{    
    int tmpvalue;
    tmpvalue = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_STATE);
    if ((tmpvalue&SII9135_PWR5V)) {
        if ( *plugin != 1) {
            *plugin = 1;
            printf("sii9135 plugin!\n");
        
        }
        return 1;
     } else {
        if ( *plugin != 0) {
            *plugin = 0;
            printf("sii9135 unplug!\n");

        }
    }    
    tmpvalue = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_SRST);
//    printf("SRST=0x%x\n", tmpvalue[0]);
    if (tmpvalue != ddr1_reg_value[0][1]) {
    //    sii9135_reset = 1;
        return 0;
    }
    return 0;
}

int sii9135_wait4vsync(int simulate, int fd_i2c)
{
    int i=10;
    int tmpvalue;
    while (i--) {      
        usleep(200000);
        tmpvalue = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_INTR2);
        printf("sync=0x%x\n", tmpvalue);
        if ((tmpvalue&SII9135_INTR2_VSYNCDET)) {
            tmpvalue = SII9135_INTR2_VSYNCDET;
            sii9135_i2c_write(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_INTR2, tmpvalue);
            return 1;
        }         
    }    
    return -1;
}
int sii9135_scdt_detect(int simulate, int fd_i2c)
{
    int ret = 0;
    int tmpvalue;
    tmpvalue = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_INTR2);
    ret = (tmpvalue&SII9135_INTR2_SCDT);
    if (ret) {
        tmpvalue = SII9135_INTR2_SCDT;
        sii9135_i2c_write(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_INTR2, tmpvalue);
    }
    return ret;
}

int read_resolution(int simulate, int fd_i2c, int *w, int *h)
{
    int i;    
    int width, height;
    int tmpvalue;
    int count = 4;

    while (count--) {
    	 tmpvalue = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_DE_PIXH);
        width = (tmpvalue & 0xff)<<8;
        tmpvalue = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_DE_PIXL);
        width |= (tmpvalue & 0xff);
        tmpvalue = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_DE_LINH);
        height = (tmpvalue & 0xff)<<8;
        tmpvalue = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_DE_LINL);
        height |= (tmpvalue & 0xff);

        if (*w != width || *h != height) {
        	*w = width;
        	*h = height;
        	return 1;
        }
 /*       sii9135_i2c_read(fd_i2c, SII9135_SLAVEADDR1, SII9135_RX_VID_STAT_ADDR, tmpvalue, 1);
    	avi_interlaced = ((tmpvalue[0]&SII9135_RX_BIT_INTRKL)==SII9135_RX_BIT_INTRKL);	
     
        if (width!=act_width || height!=act_height) {  
            act_width = width;
            act_height = height; 
            if (avi_interlaced) {        
                height *= 2;   	   
        	} 
            video_width = width;
            video_height = height;             
            return 1;
        } else {
            return 0;
        }  
  */     
    }
    return -1;
}


int check_resolution_changed(int simulate, int fd_i2c, int *w, int *h)
{
    int ret = 0;
    int tmpvalue;
    tmpvalue = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_INTR5);
 //   printf("INTR5=0x%x\n", tmpvalue[0]);
    if ((tmpvalue&SII9135_VIDEOCHGMASK)) {   //resolution changed
        usleep(100000);
        ret=read_resolution(simulate, fd_i2c, w, h);
        tmpvalue = 0xFF;
        sii9135_i2c_write(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_INTR5, tmpvalue);
    }       
    return ret;
}


int sii9135_poweron(int simulate, int fd_i2c, int on, char *power)
{
    int tmpvalue;
    if (on)
        tmpvalue = SII9135_PD_TOT_POWERON;
    else
        tmpvalue = SII9135_PD_TOT_POWERDOWN;

    if (*power != tmpvalue) {
        sii9135_i2c_write(simulate, fd_i2c, SII9135_SLAVEADDR2, SII9135_PD_TOT, tmpvalue);
        *power = tmpvalue;
//        usleep(300000);
    }
    return 0;
}


int sii9135_check_video_format(int simulate, int fd_i2c, unsigned char *avformat)
{    
    int tmpvalue;
    tmpvalue = sii9135_i2c_read(simulate, fd_i2c, SII9135_SLAVEADDR2, SII9135_AVI_DATA_BATE1);

    if (*avformat != (tmpvalue&SII9135_AVI_FORMAT_MASK)) {
        *avformat = (tmpvalue&SII9135_AVI_FORMAT_MASK);
        if (*avformat == SII9135_AVI_FORMAT_RGB) {
            tmpvalue = 0xBA;
            sii9135_i2c_write(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_VID_MODE, tmpvalue);
        } else if (*avformat == SII9135_AVI_FORMAT_YUV444) {
            tmpvalue = 0xA2;
            sii9135_i2c_write(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_VID_MODE, tmpvalue);
        }
        else if (*avformat == SII9135_AVI_FORMAT_YUV422) {
            tmpvalue = 0xA0;
            sii9135_i2c_write(simulate, fd_i2c, SII9135_SLAVEADDR1, SII9135_VID_MODE, tmpvalue);
        }
    }
    return 0;
}

int sii9135_wait4_cable_plug(int simulate, int fd_i2c, char *plugin, char *power)
{
    int hdmi_stat = -1;
	sii9135_poweron(simulate, fd_i2c, 1, power);
	hdmi_stat = sii9135_check_cable_plug(simulate, fd_i2c, plugin);
	if (hdmi_stat==1) {
		return hdmi_stat;
	}
	sii9135_poweron(simulate, fd_i2c, 0, power);
   return hdmi_stat;
}
/**
 * @return: if resolotion changed return 1 else return 0;
 */
int sii9135_check_status(int simulate, int fd_i2c, int *w, int *h,
		char *format, char *plugin, char *power)
{
	if (*plugin == 0) {
		sii9135_wait4_cable_plug(simulate, fd_i2c, plugin, power);
		return 0;
	}
	sii9135_check_cable_plug(simulate, fd_i2c, plugin);

	if (*plugin == 0) {
		*w = 0;
		*h = 0;		
		return 0;
	}
  
	sii9135_check_video_format(simulate, fd_i2c, format);
	if (check_resolution_changed(simulate, fd_i2c, w, h) == 1) {
		return 1;
	}

	return 0;
}

