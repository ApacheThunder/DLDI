/*
	iointerface.c - Apache Thunder
	
	* DLDI Driver for reading fat image in card rom via direct card reads.
	* Expects a FAT image in cart rom and is read using card read commands.
	* Card read romctrl port flags can either be hardcoded or read from 0x027FFE60.
	* Default hardcoded port flag derieved from what XMENU sets for booted rom (N-Card and it's clones).
	* This is similar to fcsr DLDi driver but for certain slot-1 cards.
	  At least for certain slot 1 cards like N-Card and it's clones or for custom homebrew flashcarts that don't use a rom loader.
	  Don't expect this to work for most modern flashcarts. Use the normal NitroFS code base for that. :P
	* Driver source available via standard GPL 3.0 license.
 
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


#define BYTES_PER_READ 512
#define CARD_DATA_BLOCK_SIZE (0x200)

#define REG_ROMCTRL		(*(vu32*)0x040001A4)
#define REG_CARD_COMMAND	((vu8*)0x040001A8)

#define REG_AUXSPICNTH	(*(vu8*)0x040001A1)
#define	REG_CARD_DATA_RD	(*(vu32*)0x04100010)

#define CARD_CR1_ENABLE  0x80  // in byte 1, i.e. 0x8000
#define CARD_CR1_IRQ     0x40  // in byte 1, i.e. 0x4000
#define CARD_BUSY         (1<<31)           // when reading, still expecting incomming data?
#define CARD_DATA_READY   (1<<23)           // when reading, CARD_DATA_RD or CARD_DATA has another word of data and is good to go

#define CARD_CMD_DATA_READ      0xB7

#define CARD_ACTIVATE     (1<<31)           // when writing, get the ball rolling
#define CARD_nRESET       (1<<29)           // value on the /reset pin (1 = high out, not a reset state, 0 = low out = in reset)
#define CARD_BLK_SIZE(n)  (((n)&0x7)<<24)   // Transfer block size, (0 = None, 1..6 = (0x100 << n) bytes, 7 = 4 bytes)

#define ROMCTRLNormalFlags 0x02FFFE60 // Hardcode this if you intend to use something like hbmenu that will end up replacing the header here.

const bool useStaticPortFlags = false;
const bool fakeWriteSuccess = true;
const u32 MaxFindRange = 0x01000000;
const u32 FATIMGMAGIC = 0xAA550000;

static vu32 CachedPortFlags = 0xFFFFFFFF; // This should use a hardcoded value if using something like hbmenu that would end up replacing the header this would have been derived from.
// static vu32 CachedPortFlags = 0xB11802FE;
static ALIGN(4) u32 cardBuffer[128];
static vu32 CardReadOffset = 0x00008000;

//---------------------------------------------------------------------------------
static inline void cardWriteCommand(const u8 *command) {
//---------------------------------------------------------------------------------
	int index;

	REG_AUXSPICNTH = (CARD_CR1_ENABLE | CARD_CR1_IRQ);

	for (index = 0; index < 8; index++)REG_CARD_COMMAND[7-index] = command[index];
}

//---------------------------------------------------------------------------------
static inline void cardPolledTransfer(u32 flags, u32 *destination, u32 length, const u8 *command) {
//---------------------------------------------------------------------------------
	u32 data;
	cardWriteCommand(command);
	REG_ROMCTRL = flags;
	u32 * target = destination + length;
	do {
		// Read data if available
		if (REG_ROMCTRL & CARD_DATA_READY) {
			data=REG_CARD_DATA_RD;
			if (NULL != destination && destination < target)*destination++ = data;
		}
	} while (REG_ROMCTRL & CARD_BUSY);
}

//---------------------------------------------------------------------------------
static inline void cardParamCommand (u8 command, u32 parameter, u32 flags, u32 *destination, u32 length) {
//---------------------------------------------------------------------------------
	u8 cmdData[8];
	
	cmdData[7] = (u8) command;
	cmdData[6] = (u8) (parameter >> 24);
	cmdData[5] = (u8) (parameter >> 16);
	cmdData[4] = (u8) (parameter >>  8);
	cmdData[3] = (u8) (parameter >>  0);
	cmdData[2] = 0;
	cmdData[1] = 0;
	cmdData[0] = 0;

	cardPolledTransfer(flags, destination, length, cmdData);
}


static inline void cardRead (u32 src, u32* dest, u32 size) {
	u32 readSize;
	while (size > 0) {
		readSize = size < CARD_DATA_BLOCK_SIZE ? size : CARD_DATA_BLOCK_SIZE;
		cardParamCommand (CARD_CMD_DATA_READ, src, (CachedPortFlags &~CARD_BLK_SIZE(7)) | CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(1), dest, readSize);
		/*if (useStaticPortFlags) {
			cardParamCommand (CARD_CMD_DATA_READ, src, (CachedPortFlags &~CARD_BLK_SIZE(7)) | CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(1), dest, readSize);
		} else {
			cardParamCommand (CARD_CMD_DATA_READ, src, CachedPortFlags | CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(1), dest, readSize);
		}*/
		src += readSize;
		dest += readSize/sizeof(*dest);
		size -= readSize;
	}
}


static bool cardFindImage() {
	u32 maxRange = (MaxFindRange - 0x8000); // 16MB max read range minus the first 0x8000 block (header + NTR secure area are skipped. Can't do card reads here without card reset)
	if (maxRange < 0x8000)maxRange = 0x8000;
	while (maxRange > 0) {
		cardRead(CardReadOffset, cardBuffer, 0x200);
		if (cardBuffer[0x1FC>>2] == FATIMGMAGIC)return true;
		CardReadOffset += 0x200;
		maxRange -= 0x200;
	}
	return false;
}

/*-----------------------------------------------------------------
CF_IsInserted
Is a compact flash card inserted?
bool return OUT:  true if a CF card is inserted
-----------------------------------------------------------------*/
bool IO_IsInserted(void) { return true; }

/*-----------------------------------------------------------------
CF_StartUp
initializes the CF interface, returns true if successful,
otherwise returns false
-----------------------------------------------------------------*/
bool IO_StartUp(void) {
	if (useStaticPortFlags) {
		CachedPortFlags = (CachedPortFlags & ~CARD_BLK_SIZE(7)); // Ensure correct block size. (allows easier external edit to CachedPortFlag field)
	} else {
		// CachedPortFlags = (*(u32*)ROMCTRLNormalFlags & ~CARD_BLK_SIZE(1)); // Version for non N-Card related use cases. use 0x02FFFE60 if you expect to run this in TWL ram mode.
		CachedPortFlags = (*(u32*)ROMCTRLNormalFlags & ~CARD_BLK_SIZE(7)); // Version for non N-Card related use cases. use 0x02FFFE60 if you expect to run this in TWL ram mode.
	}
	return cardFindImage();
}


/*-----------------------------------------------------------------
CF_ClearStatus
Tries to make the CF card go back to idle mode
bool return OUT:  true if a CF card is idle
-----------------------------------------------------------------*/
bool IO_ClearStatus(void) {
	// while (REG_ROMCTRL & CARD_BUSY);
	return true;
}

/*-----------------------------------------------------------------
IO_Shutdown
unload the GBAMP CF interface
-----------------------------------------------------------------*/
bool IO_Shutdown(void) { return true; }

/*-----------------------------------------------------------------
IO_ReadSectors
Read 512 byte sector numbered "sector" into "buffer"
u32 sector IN: address of first 512 byte sector on CF card to read
u32 numSecs IN: number of 512 byte sectors to read
void* buffer OUT: pointer to 512 byte buffer to store data in
bool return OUT: true if successful
-----------------------------------------------------------------*/
bool IO_ReadSectors(u32 sector, u32 numSecs, void* buffer) {
	u32 SectorStart = ((sector + (CardReadOffset / 0x200)) * 0x200);
	cardRead(SectorStart, buffer, (numSecs * 0x200));
	return true;
}

/*-----------------------------------------------------------------
lol you can't write to rom :P
-----------------------------------------------------------------*/
bool IO_WriteSectors(u32 sector, u32 numSecs, void* buffer) { return fakeWriteSuccess; }

