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

#define true 1
#define false 0


//***************************************************
//* Error code storage
//***************************************************
int errcode;


//***************************************************
//* Command error code output
//***************************************************
void printerr() {
  
if (errcode == -1) printf(" - command timeout\n");
else printf(" - error code %02x\n",errcode);
}

//***************************************************
// Send partition start command
// 
//  code - 32-bit partition code
//  size - full size of partition to be written
// 
//*  result:
//  false - error
//  true - command accepted by modem
//***************************************************
int dload_start(uint32_t code,uint32_t size) {

uint32_t iolen;  
uint8_t replybuf[4096];
  
#ifndef WIN32
static struct __attribute__ ((__packed__))  {
#else
#pragma pack(push,1)
static struct {
#endif
  uint8_t cmd;
  uint32_t code;
  uint32_t size;
  uint8_t pool[3];
} cmd_dload_init =  {0x41,0,0,{0,0,0}};
#ifdef WIN32
#pragma pack(pop)
#endif


cmd_dload_init.code=htonl(code);
cmd_dload_init.size=htonl(size);
iolen=send_cmd((uint8_t*)&cmd_dload_init,sizeof(cmd_dload_init),replybuf);
errcode=replybuf[3];
if ((iolen == 0) || (replybuf[1] != 2)) {
  if (iolen == 0) errcode=-1;
  return false;
}  
else return true;
}  

//***************************************************
// Send partition block
// 
//  blk - block #
//  pimage - start address of partition image in memory
// 
//*  result:
//  false - error
//  true - command accepted by modem
//***************************************************
int dload_block(uint32_t part, uint32_t blk, uint8_t* pimage) {

uint32_t res,blksize,iolen;
uint8_t replybuf[4096];

#ifndef WIN32
static struct __attribute__ ((__packed__)) {
#else
#pragma pack(push,1)
static struct {
#endif
  uint8_t cmd;
  uint32_t blk;
  uint16_t bsize;
  uint8_t data[fblock];
} cmd_dload_block;  
#ifdef WIN32
#pragma pack(pop)
#endif

blksize=fblock; // initial block size value
res=ptable[part].hd.psize-blk*fblock;  // size of remaining piece until end of file
if (res<fblock) blksize=res;  // adjust last block size

// command code
cmd_dload_block.cmd=0x42;
// block number
cmd_dload_block.blk=htonl(blk+1);
// block size
cmd_dload_block.bsize=htons(blksize);
// data portion from partition image
memcpy(cmd_dload_block.data,pimage+blk*fblock,blksize);
// send block to modem
iolen=send_cmd((uint8_t*)&cmd_dload_block,sizeof(cmd_dload_block)-fblock+blksize,replybuf); // send command

errcode=replybuf[3];
if ((iolen == 0) || (replybuf[1] != 2))  {
  if (iolen == 0) errcode=-1;
  return false;
}
return true;
}

  
//***************************************************
// Partition write completion
// 
//  code - partition code
//  size - partition size
// 
//*  result:
//  false - error
//  true - command accepted by modem
//***************************************************
int dload_end(uint32_t code, uint32_t size) {

uint32_t iolen;
uint8_t replybuf[4096];

#ifndef WIN32
static struct __attribute__ ((__packed__)) {
#else
#pragma pack(push,1)
static struct {
#endif
  uint8_t cmd;
  uint32_t size;
  uint8_t garbage[3];
  uint32_t code;
  uint8_t garbage1[11];
} cmd_dload_end;
#ifdef WIN32
#pragma pack(pop)
#endif


cmd_dload_end.cmd=0x43;
cmd_dload_end.code=htonl(code);
cmd_dload_end.size=htonl(size);
iolen=send_cmd((uint8_t*)&cmd_dload_end,sizeof(cmd_dload_end),replybuf);
errcode=replybuf[3];
if ((iolen == 0) || (replybuf[1] != 2)) {
  if (iolen == 0) errcode=-1;
  return false;
}  
return true;
}  



//***************************************************
//* Write all partitions from table to modem
//***************************************************
void flash_all() {

int32_t part;
uint32_t blk,maxblock;

printf("\n##  ---- Partition name ---- written");
// Main partition write loop
for(part=0;part<npart;part++) {
printf("\n");  
//  printf("\n02i %s)",part,ptable[part].pname);
 // partition start command
 if (!dload_start(ptable[part].hd.code,ptable[part].hd.psize)) {
   printf("\r! Partition header %i (%s) rejected",part,ptable[part].pname);
   printerr();
   exit(-2);
 }  
    
 maxblock=(ptable[part].hd.psize+(fblock-1))/fblock; // number of blocks in partition
 // Block-by-block partition image transfer loop
 for(blk=0;blk<maxblock;blk++) {
  // Output percentage written
  printf("\r%02i  %-20s  %i%%",part,ptable[part].pname,(blk+1)*100/maxblock); 

    // Send next block
  if (!dload_block(part,blk,ptable[part].pimage)) {
   printf("\n! Block %i of partition %i (%s) rejected",blk,part,ptable[part].pname);
   printerr();
   exit(-2);
  }  
 }    

// close partition
 if (!dload_end(ptable[part].hd.code,ptable[part].hd.psize)) {
   printf("\n! Error closing partition %i (%s)",part,ptable[part].pname);
   printerr();
   exit(-2);
 }  
} // end of partition loop
}
