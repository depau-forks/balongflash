// Partition table procedures

#include <stdio.h>
#include <stdint.h>
#ifndef WIN32
#include <string.h>
#include <stdlib.h>
#else
#include <windows.h>
#include "printf.h"
#endif

#include <zlib.h>

#include "ptable.h"
#include "hdlcio.h"
#include "util.h"
#include "signver.h"

int32_t lzma_decode(uint8_t* inbuf,uint32_t fsize,uint8_t* outbuf);

//******************************************************
//*  search for partition symbolic name by its code
//******************************************************

void  find_pname(unsigned int id,unsigned char* pname) {

unsigned int j;
struct {
  char name[20];
  int code;
} pcodes[]={ 
  {"M3Boot",0x20000}, 
  {"M3Boot-ptable",0x10000}, 
  {"M3Boot_R11",0x200000}, 
  {"Ptable",0x10000},
  {"Ptable_ext_A",0x480000},
  {"Ptable_ext_B",0x490000},
  {"Fastboot",0x110000},
  {"Logo",0x130000},
  {"Kernel",0x30000},
  {"Kernel_R11",0x90000},
  {"DTS_R11",0x270000},
  {"VxWorks",0x40000},
  {"VxWorks_R11",0x220000},
  {"M3Image",0x50000},
  {"M3Image_R11",0x230000},
  {"DSP",0x60000},
  {"DSP_R11",0x240000},
  {"Nvdload",0x70000},
  {"Nvdload_R11",0x250000},
  {"Nvimg",0x80000},
  {"System",0x590000},
  {"System",0x100000},
  {"APP",0x570000}, 
  {"APP",0x5a0000}, 
  {"APP_EXT_A",0x450000}, 
  {"APP_EXT_B",0x460000},
  {"Oeminfo",0xa0000},
  {"CDROMISO",0xb0000},
  {"Oeminfo",0x550000},
  {"Oeminfo",0x510000},
  {"Oeminfo",0x1a0000},
  {"WEBUI",0x560000},
  {"WEBUI",0x5b0000},
  {"Wimaxcfg",0x170000},
  {"Wimaxcrf",0x180000},
  {"Userdata",0x190000},
  {"Online",0x1b0000},
  {"Online",0x5d0000},
  {"Online",0x5e0000},
  {"Ptable_R1",0x100},
  {"Bootloader_R1",0x101},
  {"Bootrom_R1",0x102},
  {"VxWorks_R1",0x550103},
  {"Fastboot_R1",0x104},
  {"Kernel_R1",0x105},
  {"System_R1",0x107},
  {"Nvimage_R1",0x66},
  {"WEBUI_R1",0x113},
  {"APP_R1",0x109},
  {"HIFI_R11",0x280000},
  {"Modem_fw",0x1e0000},
  {"Teeos",0x290000},
  {0,0}
};

for(j=0;pcodes[j].code != 0;j++) {
  if(pcodes[j].code == id) break;
}
if (pcodes[j].code != 0) strcpy(pname,pcodes[j].name); // name found - copy it to structure
else sprintf(pname,"U%08x",id); // name not found - substitute pseudo-name Uxxxxxxxx in big-endian format
}

//*******************************************************************
// Calculate checksum block size for partition
//*******************************************************************
uint32_t crcsize(int n) { 
  return ptable[n].hd.hdsize-sizeof(struct pheader); 
  
}

//*******************************************************************
// get partition image size
//*******************************************************************
uint32_t psize(int n) { 
  return ptable[n].hd.psize; 
  
}

//*******************************************************
//*  Calculate block checksum of header
//*******************************************************
void calc_hd_crc16(int n) { 

ptable[n].hd.crc=0;   // clear old CRC sum
ptable[n].hd.crc=crc16((uint8_t*)&ptable[n].hd,sizeof(struct pheader));   
}


//*******************************************************
//*  Calculate block checksum of partition 
//*******************************************************
void calc_crc16(int n) {
  
uint32_t csize; // checksum block size in 16-bit words
uint16_t* csblock;  // pointer to created block
uint32_t off,len;
uint32_t i;
uint32_t blocksize=ptable[n].hd.blocksize; // block size covered by checksum

// determine size and create block
csize=psize(n)/blocksize;
if (psize(n)%blocksize != 0) csize++; // This is if image size is not a multiple of blocksize
csblock=(uint16_t*)malloc(csize*2);

// checksum calculation loop
for (i=0;i<csize;i++) {
 off=i*blocksize; // offset to current block 
 len=blocksize;
 if ((ptable[n].hd.psize-off)<blocksize) len=ptable[n].hd.psize-off; // for last incomplete block 
 csblock[i]=crc16(ptable[n].pimage+off,len);
} 
// write parameters to header
if (ptable[n].csumblock != 0) free(ptable[n].csumblock); // destroy old block if it existed
ptable[n].csumblock=csblock;
ptable[n].hd.hdsize=csize*2+sizeof(struct pheader);
// recalculate header CRC
calc_hd_crc16(n);
  
}


//*******************************************************************
//* Extract partition from file and add it to partition table
//*
//  in - input firmware file
//  Position in file corresponds to beginning of partition header
//*******************************************************************
void extract(FILE* in)  {

uint16_t hcrc,crc;
uint16_t* crcblock;
uint32_t crcblocksize;
uint8_t* zbuf;
long int zlen;
int res;

ptable[npart].zflag=0; 
// read header into structure
ptable[npart].offset=ftell(in);
fread(&ptable[npart].hd,1,sizeof(struct pheader),in); // header
//  Search for partition symbolic name in table 
find_pname(ptable[npart].hd.code,ptable[npart].pname);

// load checksum block
ptable[npart].csumblock=0;  // block not created yet
crcblock=(uint16_t*)malloc(crcsize(npart)); // allocate temporary memory for loaded block
crcblocksize=crcsize(npart);
fread(crcblock,1,crcblocksize,in);

// load partition image
ptable[npart].pimage=(uint8_t*)malloc(psize(npart));
fread(ptable[npart].pimage,1,psize(npart),in);

// check header CRC
hcrc=ptable[npart].hd.crc;
ptable[npart].hd.crc=0;  // old CRC is not included in calculation
crc=crc16((uint8_t*)&ptable[npart].hd,sizeof(struct pheader));
if (crc != hcrc) {
    printf("\n! Partition %s (%02x) - header checksum error",ptable[npart].pname,ptable[npart].hd.code>>16);
    errflag=1;
}  
ptable[npart].hd.crc=crc;  // restore CRC

// calculate and check partition CRC
calc_crc16(npart);
if (crcblocksize != crcsize(npart)) {
    printf("\n! Partition %s (%02x) - incorrect checksum block size",ptable[npart].pname,ptable[npart].hd.code>>16);
    errflag=1;
}    
  
else if (memcmp(crcblock,ptable[npart].csumblock,crcblocksize) != 0) {
    printf("\n! Partition %s (%02x) - incorrect block checksum",ptable[npart].pname,ptable[npart].hd.code>>16);
    errflag=1;
}  
  
free(crcblock);


ptable[npart].ztype=' ';
// Detect zlib compression


if ((*(uint16_t*)ptable[npart].pimage) == 0xda78) {
  ptable[npart].zflag=ptable[npart].hd.psize;  // save compressed size 
  zlen=52428800;
  zbuf=malloc(zlen);  // 50M buffer
  // decompress partition image
  res=uncompress (zbuf, &zlen, ptable[npart].pimage, ptable[npart].hd.psize);
  if (res != Z_OK) {
    printf("\n! Decompression error for partition %s (%02x)\n",ptable[npart].pname,ptable[npart].hd.code>>16);
    errflag=1;
  }
  // create new partition image buffer and copy decompressed data into it
  free(ptable[npart].pimage);
  ptable[npart].pimage=malloc(zlen);
  memcpy(ptable[npart].pimage,zbuf,zlen);
  ptable[npart].hd.psize=zlen;
  free(zbuf);
  // recalculate checksums
  calc_crc16(npart);
  ptable[npart].hd.crc=crc16((uint8_t*)&ptable[npart].hd,sizeof(struct pheader));
  ptable[npart].ztype='Z';
}

// Detect lzma compression

if ((ptable[npart].pimage[0] == 0x5d) && (*(uint64_t*)(ptable[npart].pimage+5) == 0xffffffffffffffff)) {
  ptable[npart].zflag=ptable[npart].hd.psize;  // save compressed size 
  zlen=100 * 1024 * 1024;
  zbuf=malloc(zlen);  // 100M buffer
  // decompress partition image
  zlen=lzma_decode(ptable[npart].pimage, ptable[npart].hd.psize, zbuf);
  if (zlen>100 * 1024 * 1024) {
    printf("\n Buffer size exceeded\n");
    exit(1);
  }  
  if (res == -1) {
    printf("\n! Decompression error for partition %s (%02x)\n",ptable[npart].pname,ptable[npart].hd.code>>16);
    errflag=1;
  }
  // create new partition image buffer and copy decompressed data into it
  free(ptable[npart].pimage);
  ptable[npart].pimage=malloc(zlen);
  memcpy(ptable[npart].pimage,zbuf,zlen);
  ptable[npart].hd.psize=zlen;
  free(zbuf);
  // recalculate checksums
  calc_crc16(npart);
  ptable[npart].hd.crc=crc16((uint8_t*)&ptable[npart].hd,sizeof(struct pheader));
  ptable[npart].ztype='L';
}
  
  
// advance partition counter
npart++;

// move forward if necessary to word boundary
res=ftell(in);
if ((res&3) != 0) fseek(in,(res+4)&(~3),SEEK_SET);
}


//*******************************************************
//*  Search for partitions in firmware file
//* 
//* returns number of found partitions
//*******************************************************
int findparts(FILE* in) {

// BIN-file prefix buffer
uint8_t prefix[0x5c];
int32_t signsize;
int32_t hd_dload_id;

// Partition header start marker	      
const unsigned int dpattern=0xa55aaa55;
unsigned int i;


// search for beginning of partition chain in file
while (fread(&i,1,4,in) == 4) {
  if (i == dpattern) break;
}
if (feof(in)) {
  printf("\n No partitions found in file - file does not contain firmware image\n");
  exit(0);
}  

// current position in file should be no closer than 0x60 from beginning - size of entire file header
if (ftell(in)<0x60) {
    printf("\n File header has incorrect size\n");
    exit(0);
}    
fseek(in,-0x60,SEEK_CUR); // move back to beginning of BIN file

// extract prefix
fread(prefix,0x5c,1,in);
hd_dload_id=prefix[0];
// if dload_id not forcibly set - select it from header
if (dload_id == -1) dload_id=hd_dload_id;
if (dload_id > 0xf) {
  printf("\n Invalid firmware type code (dload_id) in header - %x",dload_id);
  exit(0);
}  
printf("\n Firmware file code: %x (%s)\n",hd_dload_id,fw_description(hd_dload_id));

// search for remaining partitions

do {
  printf("\r Searching for partition # %i",npart); fflush(stdout);	
  if (fread(&i,1,4,in) != 4) break; // end of file
  if (i != dpattern) break;         // pattern not found - end of partition chain
  fseek(in,-4,SEEK_CUR);            // move back to beginning of header
  extract(in);                      // extract partition
} while(1);
printf("\r                                 \r");

// search for digital signature
signsize=serach_sign();
if (signsize == -1) printf("\n Digital signature: not found");
else {
  printf("\n Digital signature: %i bytes",signsize);
  printf("\n Public key hash: %s",signver_hash);
}
if (((signsize == -1) && (dload_id>7)) ||
    ((signsize != -1) && (dload_id<8))) 
    printf("\n ! WARNING: Presence of digital signature does not match firmware type code: %02x",dload_id);


return npart;
}


//*******************************************************
//* Search for partitions in multi-file mode
//*******************************************************
void findfiles (char* fdir) {

char filename[200];  
FILE* in;
  
printf("\n Searching for partition image files...\n\n ##   Size        ID        Name          File\n-----------------------------------------------------------------\n");

for (npart=0;npart<30;npart++) {
    if (find_file(npart, fdir, filename, &ptable[npart].hd.code, &ptable[npart].hd.psize) == 0) break; // end of search - partition with this ID not found
    // get partition symbolic name
    find_pname(ptable[npart].hd.code,ptable[npart].pname);
    printf("\n %02i  %8i  %08x  %-14.14s  %s",npart,ptable[npart].hd.psize,ptable[npart].hd.code,ptable[npart].pname,filename);fflush(stdout);
    
    // allocate memory for partition image
    ptable[npart].pimage=malloc(ptable[npart].hd.psize);
    if (ptable[npart].pimage == 0) {
      printf("\n! Memory allocation error, partition #%i, size = %i bytes\n",npart,ptable[npart].hd.psize);
      exit(0);
    }
    
    // read image into buffer
    in=fopen(filename,"rb");
    if (in == 0) {
      printf("\n Error opening file %s",filename);
      return;
    } 
    fread(ptable[npart].pimage,ptable[npart].hd.psize,1,in);
    fclose(in);
      
}
if (npart == 0) {
 printf("\n! No partition image files found in directory %s",fdir);
 exit(0);
} 
}

