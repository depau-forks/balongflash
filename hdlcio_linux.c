//  Low-level procedures for working with serial port and HDLC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>

#include "hdlcio.h"
#include "util.h"

unsigned int nand_cmd=0x1b400000;
unsigned int spp=0;
unsigned int pagesize=0;
unsigned int sectorsize=512;
unsigned int maxblock=0;     // Total number of flash blocks
char flash_mfr[30]={0};
char flash_descr[30]={0};
unsigned int oobsize=0;

static char pdev[500]; // serial port name

int siofd; // fd for working with serial port
struct termios sioparm;
//int siofd; // fd for working with serial port

//*************************************************
//*    send buffer to modem
//*************************************************
unsigned int send_unframed_buf(char* outcmdbuf, unsigned int outlen) {


tcflush(siofd,TCIOFLUSH);  // flush unread input buffer

write(siofd,"\x7e",1);  // send prefix

if (write(siofd,outcmdbuf,outlen) == 0) {   printf("\n Command write error");return 0;  }
tcdrain(siofd);  // wait for block output to complete

return 1;
}

//******************************************************************************************
//* Receive response buffer from modem
//*
//*  masslen - number of bytes received as a single block without analyzing end marker 7F
//******************************************************************************************

unsigned int receive_reply(char* iobuf, int masslen) {
  
int i,iolen,escflag,incount;
unsigned char c;
unsigned int res;
unsigned char replybuf[14000];

incount=0;
if (read(siofd,&c,1) != 1) {
//  printf("\n No response from modem");
  return 0; // modem did not respond or responded incorrectly
}
//if (c != 0x7e) {
//  printf("\n First byte of response - not 7e: %02x",c);
//  return 0; // modem did not respond or responded incorrectly
//}
replybuf[incount++]=c;

// read data array as single block when processing command 03
if (masslen != 0) {
 res=read(siofd,replybuf+1,masslen-1);
 if (res != (masslen-1)) {
   printf("\nResponse from modem too short: %i bytes, expected %i bytes\n",res+1,masslen);
   dump(replybuf,res+1,0);
   return 0;
 }  
 incount+=masslen-1; // we already have masslen bytes in buffer
// printf("\n ------ it mass --------");
// dump(replybuf,incount,0);
}

// receive remaining buffer tail
while (read(siofd,&c,1) == 1)  {
 replybuf[incount++]=c;
// printf("\n-- %02x",c);
 if (c == 0x7e) break;
}

// Transform received buffer to remove ESC characters
escflag=0;
iolen=0;
for (i=0;i<incount;i++) { 
  c=replybuf[i];
  if ((c == 0x7e)&&(iolen != 0)) {
    iobuf[iolen++]=0x7e;
    break;
  }  
  if (c == 0x7d) {
    escflag=1;
    continue;
  }
  if (escflag == 1) { 
    c|=0x20;
    escflag=0;
  }  
  iobuf[iolen++]=c;
}  
return iolen;

}

//***********************************************************
//* Transform command buffer with Escape substitution
//***********************************************************
unsigned int convert_cmdbuf(char* incmdbuf, int blen, char* outcmdbuf) {

int i,iolen,bcnt;
unsigned char cmdbuf[14096];

bcnt=blen;
memcpy(cmdbuf,incmdbuf,blen);
// Write CRC at end of buffer
*((unsigned short*)(cmdbuf+bcnt))=crc16(cmdbuf,bcnt);
bcnt+=2;

// Transform data with ESC-sequence escaping
iolen=0;
outcmdbuf[iolen++]=cmdbuf[0];  // copy first byte without modifications
for(i=1;i<bcnt;i++) {
   switch (cmdbuf[i]) {
     case 0x7e:
       outcmdbuf[iolen++]=0x7d;
       outcmdbuf[iolen++]=0x5e;
       break;
      
     case 0x7d:
       outcmdbuf[iolen++]=0x7d;
       outcmdbuf[iolen++]=0x5d;
       break;
      
     default:
       outcmdbuf[iolen++]=cmdbuf[i];
   }
 }
outcmdbuf[iolen++]=0x7e; // terminating byte
outcmdbuf[iolen]=0;
return iolen;
}

//***************************************************
//*  Send command to port and get result  *
//***************************************************
int send_cmd(unsigned char* incmdbuf, int blen, unsigned char* iobuf) {
  
unsigned char outcmdbuf[14096];
unsigned int  iolen;

iolen=convert_cmdbuf(incmdbuf,blen,outcmdbuf);  
if (!send_unframed_buf(outcmdbuf,iolen)) return 0; // command transmission error
return receive_reply(iobuf,0);
}

//***************************************************
// Open and configure serial port
//***************************************************

int open_port(char* devname) {


int i,dflag=1;
char devstr[200]={0};


if (strlen(devname) != 0) strcpy(pdev,devname);   // save port name  
else strcpy(devname,"/dev/ttyUSB0");  // if port name was not specified

// Instead of full device name, only the ttyUSB port number may be passed

// Check device name for non-digit characters
for(i=0;i<strlen(devname);i++) {
  if ((devname[i]<'0') || (devname[i]>'9')) dflag=0;
}
// If string contains only digits, add /dev/ttyUSB prefix

if (dflag) strcpy(devstr,"/dev/ttyUSB");

// copy device name
strcat(devstr,devname);

siofd = open(devstr, O_RDWR | O_NOCTTY |O_SYNC);
if (siofd == -1) {
  printf("\n! - Serial port %s cannot be opened\n", devname); 
  exit(0);
}
bzero(&sioparm, sizeof(sioparm)); // prepare termios attribute block
sioparm.c_cflag = B115200 | CS8 | CLOCAL | CREAD ;
sioparm.c_iflag = 0;  // INPCK;
sioparm.c_oflag = 0;
sioparm.c_lflag = 0;
sioparm.c_cc[VTIME]=30; // timeout  
sioparm.c_cc[VMIN]=0;  
tcsetattr(siofd, TCSANOW, &sioparm);

tcflush(siofd,TCIOFLUSH);  // clear output buffer

return 1;
}


//*************************************
// Configure port timeout
//*************************************

void port_timeout(int timeout) {

bzero(&sioparm, sizeof(sioparm)); // prepare termios attribute block
sioparm.c_cflag = B115200 | CS8 | CLOCAL | CREAD ;
sioparm.c_iflag = 0;  // INPCK;
sioparm.c_oflag = 0;
sioparm.c_lflag = 0;
sioparm.c_cc[VTIME]=timeout; // timeout  
sioparm.c_cc[VMIN]=0;  
tcsetattr(siofd, TCSANOW, &sioparm);
}

//*************************************************
//*  Find file by number in specified directory
//*
//* num - file #
//* filename - buffer for full file name
//* id - variable where partition identifier will be written
//*
//* return 0 - not found
//*        1 - found
//*************************************************
int find_file(int num, char* dirname, char* filename,unsigned int* id, unsigned int* size) {

DIR* fdir;
FILE* in;
unsigned int pt;
struct dirent* dentry;
char fpattern[5];

sprintf(fpattern,"%02i",num); // pattern to search file by 3 digit number
fdir=opendir(dirname);
if (fdir == 0) {
  printf("\n Directory %s cannot be opened\n",dirname);
  exit(1);
}

// main loop - search for the file we need
while ((dentry=readdir(fdir)) != 0) {
  if (dentry->d_type != DT_REG) continue; // skip all except regular files
  if (strncmp(dentry->d_name,fpattern,2) == 0) break; // found the file we need. More precisely, a file with the required 3 digits at the beginning of the name.
}

closedir(fdir);
// form full file name in result buffer
if (dentry == 0) return 0; // not found
strcpy(filename,dirname);
strcat(filename,"/");
// copy file name to result buffer
strcat(filename,dentry->d_name);  

// 00-00000200-M3Boot.bin
// check file name for presence of '-' characters
if ((dentry->d_name[2] != '-') || (dentry->d_name[11] != '-')) {
  printf("\n Incorrect file name format - %s\n",dentry->d_name);
  exit(1);
}

// check partition ID digit field
if (strspn(dentry->d_name+3,"0123456789AaBbCcDdEeFf") != 8) {
  printf("\n Error in partition identifier - non-digit character - %s\n",filename);
  exit(1);
}  
sscanf(dentry->d_name+3,"%8x",id);

// Check file accessibility and readability
in=fopen(filename,"r");
if (in == 0) {
  printf("\n Error opening file %s\n",filename);
  exit(1);
}
if (fread(&pt,1,4,in) != 4) {
  printf("\n Error reading file %s\n",filename);
  exit(1);
}
  
// check that file is raw image, without header
if (pt == 0xa55aaa55) {
  printf("\n File %s has a header - not suitable for flashing\n",filename);
  exit(1);
}


// What else can be checked? Haven't thought of anything yet.  

//  Get file size
fseek(in,0,SEEK_END);
*size=ftell(in);
fclose(in);

return 1;
}

//****************************************************
//*  Send AT command to modem
//*  
//* cmd - command buffer
//* rbuf - buffer for response
//*
//* Returns response length
//****************************************************
int atcmd(char* cmd, char* rbuf) {

int res;
char cbuf[128];

strcpy(cbuf,"AT");
strcat(cbuf,cmd);
strcat(cbuf,"\r");

port_timeout(100);
// Clean receiver and transmitter buffer
tcflush(siofd,TCIOFLUSH);

// send command
write(siofd,cbuf,strlen(cbuf));
usleep(100000);

// read result
res=read(siofd,rbuf,200);
return res;
}
  
