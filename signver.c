// 

// Digital signature processing procedures
// 
#include <stdio.h>
#include <stdint.h>
#ifndef WIN32
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#else
#include <windows.h>
#include "getopt.h"
#include "printf.h"
#include "buildno.h"
#endif

#include "hdlcio.h"
#include "ptable.h"
#include "flasher.h"
#include "util.h"
#include "zlib.h"

// -g key parameter table

struct {
  uint8_t type;
  uint32_t len;
  char* descr;
} signbase[] = {
  {1,2958,"Main firmware"},
  {1,2694,"E3372s-stick firmware"},
  {2,1110,"Webinterface+ISO for HLINK modem"},
  {6,1110,"Webinterface+ISO for HLINK modem"},
  {2,846,"ISO (dashboard) for stick modem"},
  {7,3750,"Firmware+ISO+webinterface"},
};

#define signbaselen 6

// signature type table
char* fwtypes[]={
"UNKNOWN",        // 0
"ONLY_FW",        // 1
"ONLY_ISO",       // 2
"FW_ISO",         // 3
"ONLY_WEBUI",     // 4
"FW_WEBUI",       // 5
"ISO_WEBUI",      // 6
"FW_ISO_WEBUI"    // 7
};  


// resulting ^signver command string
uint8_t signver[200];

// Digital signature mode flag
extern int gflag;

// Firmware type flag
extern int dflag;

// Current digital signature parameters
uint32_t signtype; // firmware type
uint32_t signlen;  // signature length

int32_t serach_sign();

// Public key hash for ^signver
char signver_hash[100]="778A8D175E602B7B779D9E05C330B5279B0661BF2EED99A20445B366D63DD697";

//****************************************************
//* Get firmware type description by code
//****************************************************
char* fw_description(uint8_t code) {
  
return fwtypes[code&0x7];  
}

//****************************************************
//* Get list of firmware types
//****************************************************
void dlist() {
  
int i;

printf("\n #  Description\n--------------------------------------");
for(i=1;i<8;i++) {
  printf("\n %i  %s",i,fw_description(i));
}
printf("\n\n");
exit(0);
}

//***************************************************
//* Process -d key parameters
//***************************************************
void dparm(char* sparm) {
  
if (dflag != 0) {
  printf("\n Duplicate -d key\n\n");
  exit(-1);
}  

if (sparm[0] == 'l') {
  dlist();
  exit(0);
}  
sscanf(sparm,"%x",&dload_id);
if ((dload_id == 0) || (dload_id >7)) {
  printf("\n Incorrect -d key value\n\n");
  exit(-1);
}
dflag=1;
}


//****************************************************
//* Get list of -g key parameters
//****************************************************
void glist() {
  
int i;
printf("\n #  length  type description \n--------------------------------------");
for (i=0; i<signbaselen; i++) {
  printf("\n%1i  %5i  %2i   %s",i,signbase[i].len,signbase[i].type,signbase[i].descr);
}
printf("\n\n You can also specify arbitrary signature parameters in format:\n  -g *,type,len\n\n");
exit(0);
}

//***************************************************
//* Process -g key parameters
//***************************************************
void gparm(char* sparm) {
  
int index;  
char* sptr;
char parm[100];


if (gflag != 0) {
  printf("\n Duplicate -g key\n\n");
  exit(-1);
}  

strcpy(parm,sparm); // local copy of parameters

if (parm[0] == 'l') {
  glist();
  exit(0);
}  

if (parm[0] == 'd') {
  // disable signature auto-detection
  gflag = -1;
  return;
} 

if (strncmp(parm,"*,",2) == 0) {
  // arbitrary parameters
  // extract length
  sptr=strrchr(parm,',');
  if (sptr == 0) goto perror;
  signlen=atoi(sptr+1);
  *sptr=0;
  // extract partition type
  sptr=strrchr(parm,',');
  if (sptr == 0) goto perror;
  signtype=atoi(sptr+1);
  if (fw_description(signtype) == 0) {
    printf("\n -g key: unknown firmware type - %i\n",signtype);
    exit(-1);
  }  
}
else {  
  index=atoi(parm);
  if (index >= signbaselen) goto perror;
  signlen=signbase[index].len;
  signtype=signbase[index].type;
}

gflag=1;
// printf("\nstr - %s",signver);
return;

perror:
 printf("\n Error in -g key parameters\n");
 exit(-1);
} 
  

//***************************************************
//* Send digital signature
//***************************************************
void send_signver() {
  
uint32_t res;
// ^signver response
unsigned char SVrsp[]={0x0d, 0x0a, 0x30, 0x0d, 0x0a, 0x0d, 0x0a, 0x4f, 0x4b, 0x0d, 0x0a};
uint8_t replybuf[200];
  
if (gflag == 0) {  
  // digital signature auto-detection
  signtype=dload_id&0x7;
  signlen=serach_sign();
  if (signlen == -1) return; // signature not found in file
}

printf("\n Digital signature mode: %s (%i bytes)",fw_description(signtype),signlen);
sprintf(signver,"^SIGNVER=%i,0,%s,%i",signtype,signver_hash,signlen);
res=atcmd(signver,replybuf);
if ( (res<sizeof(SVrsp)) || (memcmp(replybuf,SVrsp,sizeof(SVrsp)) != 0) ) {
   printf("\n ! Digital signature verification error - %02x\n",replybuf[2]);
   exit(-2);
}
}

//***************************************************
//* Search for digital signature in firmware
//***************************************************
int32_t serach_sign() {

int i,j;
uint32_t pt;
uint32_t signsize;

for (i=0;i<2;i++) {
  if (i == npart) break;
  pt=*((uint32_t*)&ptable[i].pimage[ptable[i].hd.psize-4]);
  if (pt == 0xffaaaffa) { 
    // signature found
    signsize=*((uint32_t*)&ptable[i].pimage[ptable[i].hd.psize-12]);
    // extract public key hash
    for(j=0;j<32;j++) {
     sprintf(signver_hash+2*j,"%02X",ptable[i].pimage[ptable[i].hd.psize-signsize+6+j]);
    }
    return signsize;
  }
}
// not found
return -1;
}