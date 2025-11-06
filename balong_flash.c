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
#include "signver.h"
#include "zlib.h"

// file structure error flag
unsigned int errflag=0;

// digital signature flag
int gflag=0;
// firmware type flag
int dflag=0;

// firmware type from file header
int dload_id=-1;

//***********************************************
//* Partition table
//***********************************************
struct ptb_t ptable[120];
int npart=0; // number of partitions in the table


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

int main(int argc, char* argv[]) {

unsigned int opt;
int res;
FILE* in;
char devname[50] = "";
unsigned int  mflag=0,eflag=0,rflag=0,sflag=0,nflag=0,kflag=0,fflag=0;
unsigned char fdir[40];   // directory for multi-file firmware

// command line parsing
while ((opt = getopt(argc, argv, "d:hp:mersng:kf")) != -1) {
  switch (opt) {
   case 'h': 
     
printf("\n This utility is designed for flashing modems on the Balong V7 chipset\n\n\
%s [options] <file name to load or directory name with files>\n\n\
 The following options are allowed:\n\n"
#ifndef WIN32
"-p <tty> - serial port for communication with the bootloader (default /dev/ttyUSB0)\n"
#else
"-p # - serial port number for communication with the bootloader (e.g., -p8)\n"
"  if -p option is not specified, automatic port detection is performed\n"
#endif
"-n       - multi-file flashing mode from the specified directory\n\
-g#      - set digital signature mode\n\
  -gl - parameter description\n\
  -gd - disable signature auto-detection\n\
-m       - display firmware file map and exit\n\
-e       - split firmware file into partitions without headers\n\
-s       - split firmware file into partitions with headers\n\
-k       - do not reboot modem after flashing\n\
-r       - force reboot modem without flashing partitions\n\
-f       - flash even if CRC errors exist in the source file\n\
-d#      - set firmware type (DLOAD_ID, 0..7), -dl - list of types\n\
\n",argv[0]);
    return 0;

   case 'p':
    strcpy(devname,optarg);
    break;

   case 'm':
     mflag=1;
     break;
     
   case 'n':
     nflag=1;
     break;
     
   case 'f':
     fflag=1;
     break;
     
   case 'r':
     rflag=1;
     break;
     
   case 'k':
     kflag=1;
     break;
     
   case 'e':
     eflag=1;
     break;

   case 's':
     sflag=1;
     break;

   case 'g':
     gparm(optarg);
     break;
     
   case 'd':
     dparm(optarg);
     break;
     
   case '?':
   case ':':  
     return -1;
  }
}  
printf("\n Program for flashing Balong chipset devices, V3.0.%i, (c) forth32, 2015, GNU GPLv3",BUILDNO);
#ifdef WIN32
printf("\n Port for Windows 32bit  (c) rust3028, 2016");
#endif
printf("\n--------------------------------------------------------------------------------------------------\n");

if (eflag&sflag) {
  printf("\n Options -s and -e are incompatible\n");
  return -1;
}  

if (kflag&rflag) {
  printf("\n Options -k and -r are incompatible\n");
  return -1;
}  

if (nflag&(eflag|sflag|mflag)) {
  printf("\n Option -n is incompatible with -s, -m and -e options\n");
  return -1;
}  


// ------  reboot without specifying a file
//--------------------------------------------
if ((optind>=argc)&rflag) goto sio; 


// Opening input file
//--------------------------------------------
if (optind>=argc) {
  if (nflag)
    printf("\n - Directory with files not specified\n");
  else 
    printf("\n - File name for loading not specified, use -h option for help\n");
  return -1;
}  

if (nflag) 
  // for -n - just copy the prefix
  strncpy(fdir,argv[optind],39);
else {
  // for single-file operations
in=fopen(argv[optind],"rb");
if (in == 0) {
  printf("\n Error opening %s",argv[optind]);
  return -1;
}
}


// Search for partitions inside the file
if (!nflag) {
  findparts(in);
  show_fw_info();
}  

// Search for firmware files in the specified directory
else findfiles(fdir);
  
//------ Firmware file map display mode
if (mflag) show_file_map();

// exit on CRC errors
if (!fflag && errflag) {
    printf("\n\n! Input file contains errors - terminating\n");
    return -1; 
}

//------- Firmware file split mode
if (eflag|sflag) {
  fwsplit(sflag);
  printf("\n");
  return 0;
}

sio:
//--------- Main mode - firmware writing
//--------------------------------------------

// SIO setup
open_port(devname);

// Determine port mode and dload protocol version

res=dloadversion();
if (res == -1) return -2;
if (res == 0) {
  printf("\n Modem is already in HDLC mode");
  goto hdlc;
}

// If necessary, send digital signature command
if (gflag != -1) send_signver();

// Enter HDLC mode

usleep(100000);
enter_hdlc();

// Entered HDLC
//------------------------------
hdlc:

// get protocol version and device identifier
protocol_version();
dev_ident();


printf("\n----------------------------------------------------\n");

if ((optind>=argc)&rflag) {
  // reboot without specifying a file
  restart_modem();
  exit(0);
}  

// Write all flash
flash_all();
printf("\n");

port_timeout(1);

// exit HDLC mode and reboot
if (rflag || !kflag) restart_modem();
// exit HDLC without reboot
else leave_hdlc();
} 
