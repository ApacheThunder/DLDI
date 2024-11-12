/*
	Slot2 Ram Drive - Apache Thunder
	
	* Simple Ram Drive DLDI driver for Slot2 GBA carts that support being used as ram expansion.
	* Default configuration is compatible with EZFlash 3in1 and SuperCard SD/SuperCard Lite
	* Use is3in1 bool to switch to 16MB max sector count. Otherwise leave it to false if using it on a cart that has 32MB ram.
	* An example on how to use this driver is to build a fat image, put files in it, then attach fat image to a NDS homebrew rom with pass me header added.
	* When adding fat image to NDS file, ensure fat image address in rom is divisble by 512 (0x200) to ensure it is sector aligned.
 
*/
// When compiling for NDS, make sure NDS is defined
#ifndef NDS
	#if defined ARM9 || defined ARM7
		#define NDS
	#endif
#endif

#ifdef NDS
	#include <nds/ndstypes.h>
	#include <nds/system.h>
#else
	#include "gba_types.h"
#endif

#ifndef NULL
	#define NULL 0
#endif

#include <nds.h>
#include "tonccpy.h"


#define SECTOR_SIZE		0x200 		// 512
#define gbaRamLocation	0x08000000	// GBA ROM REGION

const bool is3in1 = false;
const u32 FATIMGMAGIC = 0xAA550000;

volatile u32 ramDriveSectorCount = 0x10000; // 32MB
volatile u32 maxSectors = 0x8000;
volatile u32 ramSectorOffset = 0x261;
volatile u32 ramOffset = 0;

bool IO_StartUp() {
	ramDriveSectorCount = 0x10000;
	if (is3in1) {
		ramDriveSectorCount = 0x8000;
		maxSectors = 0x4000;
	}
	for(ramSectorOffset = 0; ramSectorOffset < maxSectors; ramSectorOffset++) {
		if (*(u32*)(gbaRamLocation + (ramSectorOffset * SECTOR_SIZE) + 0x1FC) == FATIMGMAGIC) {
			ramOffset = (ramSectorOffset * SECTOR_SIZE);
			return true;
		}
	}
	return false;
}


bool IO_ReadSectors(u32 sector, u32 numSectors, void *buffer) {
	for(u32 i = 0; i < numSectors; i++) {
		u32 curSector = (sector + i);
		if (curSector > (ramDriveSectorCount - ramSectorOffset))return false;
		tonccpy(buffer + (i * SECTOR_SIZE), (u32*)((gbaRamLocation + ramOffset) + (curSector * SECTOR_SIZE)), SECTOR_SIZE);
	}
	return true;
}

bool IO_WriteSectors(u32 sector, u32 numSectors, const void *buffer) {
	for(u32 i = 0; i < numSectors; i++) {
		u32 curSector = (sector + i);
		if (curSector > (ramDriveSectorCount - ramSectorOffset))return false;
		tonccpy((u32*)((gbaRamLocation + ramSectorOffset) + (curSector * SECTOR_SIZE)), buffer + (i * SECTOR_SIZE), SECTOR_SIZE);
	}
	return true;
}

bool IO_IsInserted() { return true; }
bool IO_ClearStatus() { return true; }
bool IO_Shutdown() { return true; }

