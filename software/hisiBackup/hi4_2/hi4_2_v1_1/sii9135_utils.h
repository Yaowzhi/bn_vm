#ifndef __SII9135_H__
#define __SII9135_H__

#define SII9135_AVI_DATA_BATE1      0x44
#define SII9135_AVI_FORMAT_MASK     0x60
#define SII9135_AVI_FORMAT_YUV444   0x40
#define SII9135_AVI_FORMAT_YUV422   0x20
#define SII9135_AVI_FORMAT_RGB      0x00

#define SII9135_PD_TOT              0x3C
#define SII9135_PD_TOT_POWERDOWN    0x00
#define SII9135_PD_TOT_POWERON      0x01

extern int sii9135_init(int slot, int simulate, int fd_i2c);
extern int sii9135_check_status(int simulate, int fd_i2c, int *w, int *h,
		char *format, char *plugin, char *power);

#endif
