/******************************************************************************
  A simple program of Hisilicon HI3531 video input and output implementation.
  Copyright (C), 2010-2011, Hisilicon Tech. Co., Ltd.
 ******************************************************************************
    Modification:  2011-8 Created
******************************************************************************/

#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "sample_comm.h"

#include<string.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<netdb.h>
#include<sys/wait.h>

#define PORT 9898
#define MAXDATASIZE 100

#define PIPE_STDIN 0
#define PIPE_STDOUT 1

VIDEO_NORM_E gs_enNorm = VIDEO_ENCODING_MODE_PAL;
SAMPLE_VIDEO_LOSS_S gs_stVideoLoss;
HI_U32 gs_u32ViFrmRate = 0; 


/******************************************************************************
* function : to process abnormal case                                         
******************************************************************************/
void SAMPLE_VIO_HandleSig(HI_S32 signo)
{
    if (SIGINT == signo || SIGTSTP == signo)
    {
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
    }
    exit(-1);
}

/******************************************************************************
* function : video loss detect process                                         
* NOTE: If your ADC stop output signal when NoVideo, you can open VDET_USE_VI macro.
******************************************************************************/
//#define VDET_USE_VI    
#ifdef VDET_USE_VI  
static HI_S32 s_astViLastIntCnt[VIU_MAX_CHN_NUM] = {0};
void *SAMPLE_VI_VLossDetProc(void *parg)
{ 
    VI_CHN ViChn;
    SAMPLE_VI_PARAM_S stViParam;
    HI_S32 s32Ret, i, s32ChnPerDev;
    VI_CHN_STAT_S stStat;
    SAMPLE_VIDEO_LOSS_S *ctl = (SAMPLE_VIDEO_LOSS_S*)parg;
    
    s32Ret = SAMPLE_COMM_VI_Mode2Param(ctl->enViMode, &stViParam);
    if (HI_SUCCESS !=s32Ret)
    {
        SAMPLE_PRT("vi get param failed!\n");
        return NULL;
    }
    s32ChnPerDev = stViParam.s32ViChnCnt / stViParam.s32ViDevCnt;
    
    while (ctl->bStart)
    {
        for (i = 0; i < stViParam.s32ViChnCnt; i++)
        {
            ViChn = i * stViParam.s32ViChnInterval;

            s32Ret = HI_MPI_VI_Query(ViChn, &stStat);
            if (HI_SUCCESS !=s32Ret)
            {
                SAMPLE_PRT("HI_MPI_VI_Query failed with %#x!\n", s32Ret);
                return NULL;
            }
            
            if (stStat.u32IntCnt == s_astViLastIntCnt[i])
            {
                HI_MPI_VI_EnableUserPic(ViChn);
            }
            else
            {
                HI_MPI_VI_DisableUserPic(ViChn);
            }
            s_astViLastIntCnt[i] = stStat.u32IntCnt;
        }
        usleep(500000);
    }
    
    ctl->bStart = HI_FALSE;
    
    return NULL;
}
#else  
void *SAMPLE_VI_VLossDetProc(void *parg)
{  
    int fd;
    HI_S32 s32Ret, i, s32ChnPerDev;
    VI_DEV ViDev;
    VI_CHN ViChn;
    tw2865_video_loss video_loss;
    SAMPLE_VI_PARAM_S stViParam;
    SAMPLE_VIDEO_LOSS_S *ctl = (SAMPLE_VIDEO_LOSS_S*)parg;
    
    fd = open(TW2865_FILE, O_RDWR);
    if (fd < 0)
    {
        printf("open %s fail\n", TW2865_FILE);
        ctl->bStart = HI_FALSE;
        return NULL;
    }

    s32Ret = SAMPLE_COMM_VI_Mode2Param(ctl->enViMode, &stViParam);
    if (HI_SUCCESS !=s32Ret)
    {
        SAMPLE_PRT("vi get param failed!\n");
        return NULL;
    }
    s32ChnPerDev = stViParam.s32ViChnCnt / stViParam.s32ViDevCnt;
    
    while (ctl->bStart)
    {
        for (i = 0; i < stViParam.s32ViChnCnt; i++)
        {
            ViChn = i * stViParam.s32ViChnInterval;
            ViDev = SAMPLE_COMM_VI_GetDev(ctl->enViMode, ViChn);
            if (ViDev < 0)
            {
                SAMPLE_PRT("get vi dev failed !\n");
                return NULL;
            }
            
            video_loss.chip = stViParam.s32ViDevCnt;
            video_loss.ch   = ViChn % s32ChnPerDev;
            ioctl(fd, TW2865_GET_VIDEO_LOSS, &video_loss);
            
            if (video_loss.is_lost)
            {
                HI_MPI_VI_EnableUserPic(ViChn);
            }
            else
            {
                HI_MPI_VI_DisableUserPic(ViChn);
            }                
        }
        usleep(500000);
    }
    
    close(fd);
    ctl->bStart = HI_FALSE;
    
    return NULL;
}
#endif  

HI_S32 SAMPLE_VI_StartVLossDet(SAMPLE_VI_MODE_E enViMode)
{
    HI_S32 s32Ret;
    
    gs_stVideoLoss.bStart= HI_TRUE;
    gs_stVideoLoss.enViMode = enViMode;
    s32Ret = pthread_create(&gs_stVideoLoss.Pid, 0, SAMPLE_VI_VLossDetProc, &gs_stVideoLoss);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("pthread_create failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    
    return HI_SUCCESS;
}

HI_VOID SAMPLE_VI_StopVLossDet()
{
    if (gs_stVideoLoss.bStart)
    {
        gs_stVideoLoss.bStart = HI_FALSE;
        pthread_join(gs_stVideoLoss.Pid, 0);
    }
    return;
}

HI_S32 SAMPLE_VI_SetUserPic(HI_CHAR *pszYuvFile, HI_U32 u32Width, HI_U32 u32Height,
        HI_U32 u32Stride, VIDEO_FRAME_INFO_S *pstFrame)
{
    FILE *pfd;
    VI_USERPIC_ATTR_S stUserPicAttr;

    /* open YUV file */
    pfd = fopen(pszYuvFile, "rb");
    if (!pfd)
    {
        printf("open file -> %s fail \n", pszYuvFile);
        return -1;
    }

    /* read YUV file. WARNING: we only support planar 420) */
    if (SAMPLE_COMM_VI_GetVFrameFromYUV(pfd, u32Width, u32Height, u32Stride, pstFrame))
    {
        return -1;
    }
    fclose(pfd);

    stUserPicAttr.bPub= HI_TRUE;
    stUserPicAttr.enUsrPicMode = VI_USERPIC_MODE_PIC;
    memcpy(&stUserPicAttr.unUsrPic.stUsrPicFrm, pstFrame, sizeof(VIDEO_FRAME_INFO_S));
    if (HI_MPI_VI_SetUserPic(0, &stUserPicAttr))
    {
        return -1;
    }

    printf("set vi user pic ok, yuvfile:%s\n", pszYuvFile);
    return HI_SUCCESS;
}





    SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_4_1080P;
    HI_U32 u32ViChnCnt = 4;
    HI_S32 s32VpssGrpCnt = 4;
    
    VB_CONF_S stVbConf;
    VPSS_GRP VpssGrp;
    VPSS_GRP_ATTR_S stGrpAttr;
    VO_DEV VoDev;
    VO_CHN VoChn;
    VI_CHN ViChn_Sub;		//for cvbs output 
    VO_PUB_ATTR_S stVoPubAttr; 
    SAMPLE_VO_MODE_E enVoMode, enPreVoMode;
    
    HI_S32 i;
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    SIZE_S stSize;
    HI_U32 u32WndNum,u32PreWndNum;

    VO_WBC_ATTR_S stWbcAttr;





HI_S32 SAMPLE_VIO_4HD_Homo_start()
{
    /******************************************
     step  1: init global  variable 
    ******************************************/
    gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL == gs_enNorm)?25:30;
    memset(&stVbConf,0,sizeof(VB_CONF_S));

    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_HD1080, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    stVbConf.u32MaxPoolCnt = 128;

    /*ddr0 video buffer*/
    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = u32ViChnCnt * 3;
    memset(stVbConf.astCommPool[0].acMmzName,0,
        sizeof(stVbConf.astCommPool[0].acMmzName));

    /*ddr1 video buffer*/
    stVbConf.astCommPool[1].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[1].u32BlkCnt = u32ViChnCnt * 3;
    strcpy(stVbConf.astCommPool[1].acMmzName,"ddr1");

    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_D1, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    
    /* WARNING : HD we will use 4*D1 VI_SUB_CHNs for CVBS Preview. */
    /* because these SUB_CHNs not pass VPSS, so needn't hist buffer.    */
    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_D1, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);

    /*ddr0 video buffer*/
    stVbConf.astCommPool[2].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[2].u32BlkCnt = u32ViChnCnt * 3;
    memset(stVbConf.astCommPool[2].acMmzName,0,
        sizeof(stVbConf.astCommPool[2].acMmzName));

    /*ddr1 video buffer*/
    stVbConf.astCommPool[3].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[3].u32BlkCnt = u32ViChnCnt * 3;
    strcpy(stVbConf.astCommPool[3].acMmzName,"ddr1");

    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d\n", s32Ret);
        return s32Ret;//goto END_4HD_HOMO_0;
    }

    s32Ret = SAMPLE_COMM_VI_MemConfig(enViMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VI_MemConfig failed with %d!\n", s32Ret);
        return s32Ret;//goto END_4HD_HOMO_0;
    }

    s32Ret = SAMPLE_COMM_VPSS_MemConfig();
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VPSS_MemConfig failed with %d!\n", s32Ret);
        return s32Ret;//goto END_4HD_HOMO_0;
    }

    s32Ret = SAMPLE_COMM_VO_MemConfig(SAMPLE_VO_DEV_DHD0, "ddr1");
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_MemConfig failed with %d!\n", s32Ret);
        return s32Ret;//goto END_4HD_HOMO_0;
    }
    
    s32Ret = SAMPLE_COMM_VO_MemConfig(SAMPLE_VO_DEV_DHD1, "ddr1");
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_MemConfig failed with %d!\n", s32Ret);
        return s32Ret;//goto END_4HD_HOMO_0;
    }
    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    s32Ret = SAMPLE_COMM_VI_Start(enViMode, gs_enNorm);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        return s32Ret;//goto END_4HD_HOMO_0;
    }
    
    /******************************************
     step 4: start vpss and vi bind vpss (subchn needn't bind vpss in this mode)
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_HD1080, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        return s32Ret;//goto END_4HD_HOMO_0;
    }

    stGrpAttr.u32MaxW = stSize.u32Width;
    stGrpAttr.u32MaxH = stSize.u32Height;
    stGrpAttr.bDrEn = HI_FALSE;
    stGrpAttr.bDbEn = HI_FALSE;
    stGrpAttr.bIeEn = HI_TRUE;
    stGrpAttr.bNrEn = HI_TRUE;
    stGrpAttr.bHistEn = HI_TRUE;
    stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
    stGrpAttr.enPixFmt = SAMPLE_PIXEL_FORMAT;

    s32Ret = SAMPLE_COMM_VPSS_Start(s32VpssGrpCnt, &stSize, VPSS_MAX_CHN_NUM,NULL);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Vpss failed!\n");
        return s32Ret;//goto END_4HD_HOMO_1;
    }

    s32Ret = SAMPLE_COMM_VI_BindVpss(enViMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Vi bind Vpss failed!\n");
        return s32Ret;//goto END_4HD_HOMO_2;
    }
        
    return s32Ret;
}




HI_S32 SAMPLE_VIO_4HD_Homo(HI_CHAR Input,HI_CHAR Output)
{

    if(Output == '0')
    {
        VoDev = SAMPLE_VO_DEV_DHD0;
    }
    if(Output == '1')
    {
        VoDev = SAMPLE_VO_DEV_DHD1;
    }

        /******************************************
         step 5: start vo HD1 (VGA+HDMI) (WBC source) 
        ******************************************/
        
	if(Output == '0')
        {
        stVoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
        stVoPubAttr.enIntfType = VO_INTF_VGA ; // VGA output
        stVoPubAttr.u32BgColor = 0x000000ff;
        stVoPubAttr.bDoubleFrame = HI_FALSE;  // In VI HD input case, this item should be set to HI_FALSE
        printf("vo HD0:VGA\n");
	}
	if(Output == '1')	
	{
        stVoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
        stVoPubAttr.enIntfType = VO_INTF_HDMI ;//HDMI output
        stVoPubAttr.u32BgColor = 0x000000ff;
        stVoPubAttr.bDoubleFrame = HI_FALSE;  // In VI HD input case, this item should be set to HI_FALSE
        printf("vo HD1:HDMI\n");
	}

        u32WndNum = 4;
        enVoMode = VO_MODE_1MUX;

    s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartDevLayer failed!\n");
        return s32Ret;//goto END_4HD_HOMO_6;
    }
    s32Ret = SAMPLE_COMM_VO_StartChn_V2(VoDev, &stVoPubAttr, VO_MODE_1MUX, Input);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed!\n");
        return s32Ret;//goto END_4HD_HOMO_6;
    }
    /* if it's displayed on HDMI, we should start HDMI */
    if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
    {
        if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(stVoPubAttr.enIntfSync))
        {
            SAMPLE_PRT("Start SAMPLE_COMM_VO_HdmiStart failed!\n");
            return s32Ret;//goto END_4HD_HOMO_6;
        }
    }
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        VpssGrp = i;
        s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev,VoChn,VpssGrp,VPSS_PRE0_CHN);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("SAMPLE_COMM_VO_BindVpss failed!\n");
          return s32Ret; // goto END_4HD_HOMO_6;
        }
    }

    
    return s32Ret;
}




HI_S32 SAMPLE_VIO_4HD_Homo_end(HI_CHAR Input,HI_CHAR Output)
{

        u32WndNum = 4;
        enVoMode = VO_MODE_1MUX;
    if(Output == '0')
    {
        VoDev = SAMPLE_VO_DEV_DHD0;
    }
    if(Output == '1')
    {
        VoDev = SAMPLE_VO_DEV_DHD1;
    }

    if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
    {
        SAMPLE_COMM_VO_HdmiStop();
    }
    SAMPLE_COMM_VO_StopChn_V2(VoDev, enVoMode,Input);
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        VpssGrp = i;
        SAMPLE_COMM_VO_UnBindVpss(VoDev,VoChn,VpssGrp,VPSS_PRE0_CHN);
    }
    SAMPLE_COMM_VO_StopDevLayer(VoDev);
 
    return s32Ret;
}





/******************************************************************************
* function    : main()
* Description : video preview sample
******************************************************************************/
int main(int argc, char *argv[])
{
    	//HI_S32 s32Ret;
        
	int ret,myPipe[2];
	HI_S32 s32Ret;
	ret=pipe(myPipe);
	if(ret!=0)  exit(ret);
	pid_t pid;
	if((pid=fork())==-1) {printf("Error in fork\n");exit(1);}

	if(pid==0)
	{
		char Lastbuffer_1=0;
		char Lastbuffer_2=0;
		char buffer[6];

    		signal(SIGINT, SAMPLE_VIO_HandleSig);
	    	signal(SIGTERM, SAMPLE_VIO_HandleSig);

	        SAMPLE_VIO_4HD_Homo_start();	
       		//close(myPipe[PIPE_STDOUT]);
       		read(myPipe[PIPE_STDIN],buffer,(sizeof(buffer) - 1));
		//close(myPipe[PIPE_STDIN]);

		while(1)
		{

                		        /*SAMPLE_VIO_4HD_Homo('0','1');
					getchar();
					
       					//close(myPipe[PIPE_STDOUT]);
       		    			read(myPipe[PIPE_STDIN],buffer,(sizeof(buffer) - 1));
					//close(myPipe[PIPE_STDIN]);
					SAMPLE_VIO_4HD_Homo_end('0','1');
					//	getchar();
				
                		        SAMPLE_VIO_4HD_Homo('1','1');
					getchar();
			
       					//close(myPipe[PIPE_STDOUT]);
       					//read(myPipe[PIPE_STDIN],buffer,(sizeof(buffer) - 1));
					//close(myPipe[PIPE_STDIN]);
					SAMPLE_VIO_4HD_Homo_end('1','1');
		 			//getchar();*/


			if((buffer[0]=='e')&&(buffer[1]=='x')&&(buffer[2]=='i')&&(buffer[3]=='t'))	
			{	
				printf("exit !\n");
                                if((Lastbuffer_1 !=0) && (Lastbuffer_2 !=0))
				{
					SAMPLE_VIO_4HD_Homo_end((Lastbuffer_1 - 1),(Lastbuffer_2 - 1));
				}
    				SAMPLE_COMM_VI_UnBindVpss(enViMode);
    				SAMPLE_COMM_VPSS_Stop(s32VpssGrpCnt, VPSS_MAX_CHN_NUM);
    				SAMPLE_COMM_VI_Stop(enViMode);
    				SAMPLE_COMM_SYS_Exit();
				
				exit(0);
			}

			if((buffer[0]=='<')&&(buffer[4]=='>'))
			{
				buffer[5]='\0';
				printf("\nbuffer:%s\n",buffer);
				if((Lastbuffer_1 != buffer[1]) || (Lastbuffer_2 != buffer[2]))
			        {	
                                     if((Lastbuffer_1 !=0) && (Lastbuffer_2 !=0))
			 	     {
					printf("Lastbuffe_1 - 1:%c  ,Lastbuffer_2 - 1:%c\n",(Lastbuffer_1 - 1),(Lastbuffer_2 - 1)); 
					SAMPLE_VIO_4HD_Homo_end((Lastbuffer_1 - 1),(Lastbuffer_2 - 1));
					
				     }
					printf("buffer[1] -1 :%c,buffer[2] -1:%c\n",(buffer[1] -1),(buffer[2] -1));
                		     s32Ret = SAMPLE_VIO_4HD_Homo((buffer[1] - 1),(buffer[2] - 1));
    				     if (HI_SUCCESS == s32Ret)
        				   printf("display success.\n");
    				     else
        			     	   printf("display failed.\n");
				     Lastbuffer_1=buffer[1];Lastbuffer_2=buffer[2];
				     bzero(buffer,sizeof(buffer));
				}
			}

       			//close(myPipe[PIPE_STDOUT]);
       			read(myPipe[PIPE_STDIN],buffer,(sizeof(buffer) - 1));
			
		}
		
	}
	else if(pid>0)
	{
		
	int sockfd;
	struct sockaddr_in server;
	struct sockaddr_in client;
	socklen_t addrlen;
	int num;
	char buf[MAXDATASIZE];


	if((sockfd=socket(AF_INET,SOCK_DGRAM,0))==-1)
	{
		perror("Creating socket failed.");
		exit(1);
	}

	bzero(&server,sizeof(server));
	server.sin_family=AF_INET;
	server.sin_port=htons(PORT);
	server.sin_addr.s_addr=htonl(INADDR_ANY);

	if(bind(sockfd,(struct sockaddr*)&server,sizeof(server))==-1)
	{
		perror("Bind() error.");
		exit(1);
	}

	addrlen=sizeof(client);
		while(1)
		{
			num=recvfrom(sockfd,buf,MAXDATASIZE,0,(struct sockaddr*)&client,&addrlen);
			if(num<0)
			{
				perror("recvfrom() error\n");
				exit(1);
			}

			buf[num]='\0';
			printf("You got a message %s from client.\nIt's ip is %s,port is %d.\n",buf,inet_ntoa(client.sin_addr),htons(client.sin_port));
  			if((buf[0]=='<')&&(buf[4]=='>'))
			sendto(sockfd,"cmd valid!\n",11,0,(struct sockaddr*)&client,addrlen);
			if((buf[0]=='e')&&(buf[1]=='x')&&(buf[2]=='i')&&(buf[3]=='t'))
			sendto(sockfd,"exit ok!\n",9,0,(struct sockaddr*)&client,addrlen);

			//if(!strcmp(buf,"exit"))
			//wait(NULL);

			close(myPipe[PIPE_STDIN]);
			write(myPipe[PIPE_STDOUT],buf,strlen(buf));
			
				
			if(!strcmp(buf,"exit"))
			break;//wait(NULL);
		}		
		wait(NULL);
		close(sockfd);   
	}	

		return 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

