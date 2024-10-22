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


#include "cardme.h"
#include "tonccpy.h"

#define CARD_DATA_BLOCK_SIZE 0x200

#define REG_ROMCTRL			(*(vu32*)0x040001A4)
#define REG_CARD_COMMAND	((vu8*)0x040001A8)

#define REG_AUXSPICNTH		(*(vu8*)0x040001A1)
#define	REG_CARD_DATA_RD	(*(vu32*)0x04100010)

#define CARD_CMD_DATA_READ	0xB7
#define CARD_CR1_ENABLE		0x80  // in byte 1, i.e. 0x8000
#define CARD_CR1_IRQ		0x40  // in byte 1, i.e. 0x4000
#define CARD_BUSY			(1<<31)           // when reading, still expecting incomming data?
#define CARD_DATA_READY		(1<<23)           // when reading, CARD_DATA_RD or CARD_DATA has another word of data and is good to go

#define CARD_ACTIVATE		(1<<31)           // when writing, get the ball rolling
#define CARD_nRESET			(1<<29)           // value on the /reset pin (1 = high out, not a reset state, 0 = low out = in reset)
#define CARD_BLK_SIZE(n)	(((n)&0x7)<<24)   // Transfer block size, (0 = None, 1..6 = (0x100 << n) bytes, 7 = 4 bytes)

#define ROMCTRLNormalFlags 0x02FFFE60 // Hardcode this if you intend to use something like hbmenu that will end up replacing the header here.

// Use this to use hardcoded port flags and fat image sector offset. (find rom address offset of fat image and divide it by 0x200 to get sector offset)
// Use this if using something like hbmenu which will overwrite header data upon booting something else within the fat image.
const bool useStaticPortFlags = false;
const bool fakeWriteSuccess = true;
const bool useSaveSystem = true;
// const u32 StaticPortFlags = 0x00586000; // Value used by EZP main rom
const u32 StaticPortFlags = 0xB11802FE; // Value hard-coded by XMENU on N-Cards
const u32 StaticSectorOffset = 0x00000256;
// const u32 StaticSectorOffset = 0x00000182;
// const u32 StaticSectorOffset = 0x00000040;

const u32 MaxFindRange = 0x01000000;
const u32 FATIMGMAGIC = 0xAA550000;
const u32 BufferTableMagic = 0x4F52544E;
const u32 BufferTableMaxSize = 0x1FA;

static vu32 CachedPortFlags = 0xFFFFFFFF; // This should use a hardcoded value if using something like hbmenu that would end up replacing the header this would have been derived from.
static vu32 CardReadOffset = 0x00008000;
static vu32 CachedSectorOffset = 0x00000040;

static bool hasEnoughSaveData = true;
static vu32 savetype = 0;
static vu32 savesize = 0;
static vu32 bufferTableSize = 0;

static ALIGN(16) u32 cardBuffer[128];
static ALIGN(16) u32 EEPROMBuffer[512];



static inline void cardWriteCommand(const u8 *command) {
	int index;

	REG_AUXSPICNTH = (CARD_CR1_ENABLE | CARD_CR1_IRQ);

	for (index = 0; index < 8; index++)REG_CARD_COMMAND[7-index] = command[index];
}

static inline void cardPolledTransfer(u32 flags, u32 *destination, u32 length, const u8 *command) {
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

static inline void cardParamCommand (u8 command, u32 parameter, u32 flags, u32 *destination, u32 length) {
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


static inline bool cardRead (u32 src, u32* dest, u32 size) {
	u32 readSize;
	while (size > 0) {
		readSize = size < CARD_DATA_BLOCK_SIZE ? size : CARD_DATA_BLOCK_SIZE;
		cardParamCommand (CARD_CMD_DATA_READ, src, (CachedPortFlags &~CARD_BLK_SIZE(7)) | CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(1), dest, readSize);
		src += readSize;
		dest += readSize/sizeof(*dest);
		size -= readSize;
	}
	return true;
}

static inline void BuildEEPROMBuffer() {
	if (!useSaveSystem) {
		hasEnoughSaveData = false;
		return;
	}
	
	savetype = cardmeGetType();
	
	if(savetype > 0)savesize = cardmeSize(savetype);
	
	if (savesize < 0x3F600) {
		hasEnoughSaveData = false;
		return;
	}
	
	cardmeReadEeprom(0, (u8*)&EEPROMBuffer, (CARD_DATA_BLOCK_SIZE * 4), savetype);
	if (EEPROMBuffer[0] != BufferTableMagic) {
		toncset32(EEPROMBuffer, 0xFFFFFFFF, 512);
		EEPROMBuffer[0] = BufferTableMagic;
		bufferTableSize = 0;
		// Disabled for now. N-Card doesn't seem to require page/sector erases.
		// It uses battery backed SRAM and allows writes to unerased blocks even though it shows up as type 3.
		/*switch (savetype) {
			case 3: for (int i = 0; i < 8; i++)cardmePageErase(i * 256); break;
		}*/
		cardmeWriteEeprom(0, (u8*)EEPROMBuffer, (CARD_DATA_BLOCK_SIZE * 4), savetype);
	} else {
		for (int i = 1; i < BufferTableMaxSize; i++) {
			if (EEPROMBuffer[i] == 0xFFFFFFFF)break;
			bufferTableSize++;
		}
	}
}

bool IO_StartUp(void) {
	if (useStaticPortFlags) {
		CachedPortFlags = (StaticPortFlags & ~CARD_BLK_SIZE(7)); // Ensure correct block size. (allows easier external edit to CachedPortFlag field)
		CachedSectorOffset = StaticSectorOffset;
		BuildEEPROMBuffer();
		return useStaticPortFlags;
	} else {
		CachedPortFlags = (*(u32*)ROMCTRLNormalFlags & ~CARD_BLK_SIZE(7)); // Version for non N-Card related use cases. use 0x02FFFE60 if you expect to run this in TWL ram mode.
	}
	u32 maxRange = (MaxFindRange - 0x8000); // 16MB max read range minus the first 0x8000 block (header + NTR secure area are skipped. Can't do card reads here without card reset)
	if (maxRange < 0x8000)maxRange = 0x8000;
	while (maxRange > 0) {
		cardRead(CardReadOffset, cardBuffer, CARD_DATA_BLOCK_SIZE);
		if (cardBuffer[0x1FC>>2] == FATIMGMAGIC) {
			CachedSectorOffset = (CardReadOffset / CARD_DATA_BLOCK_SIZE);
			BuildEEPROMBuffer();
			return true;
		}
		CardReadOffset += CARD_DATA_BLOCK_SIZE;
		maxRange -= CARD_DATA_BLOCK_SIZE;
	}
	return false;
}


/*-----------------------------------------------------------------
IO_ReadSectors
Read 512 byte sector numbered "sector" into "buffer"
u32 sector IN: address of first 512 byte sector on CF card to read
u32 numSecs IN: number of 512 byte sectors to read
void* buffer OUT: pointer to 512 byte buffer to store data in
bool return OUT: true if successful
-----------------------------------------------------------------*/
bool IO_ReadSectors(u32 sector, u32 numSecs, u32* buffer) {
	for(u32 I = 0; I < numSecs; I++) {
		bool readCachedSector = false;
		if ((bufferTableSize > 0) && hasEnoughSaveData) {
			for (int i = 1; i < bufferTableSize; i++) {
				if (EEPROMBuffer[i] == (sector + I)) {
					cardmeReadEeprom(((i + 4) * CARD_DATA_BLOCK_SIZE), (u8*)&buffer[(I * 128)], CARD_DATA_BLOCK_SIZE, savetype);
					readCachedSector = true;
					break;
				}
			}
		}
		if (!readCachedSector)cardParamCommand(CARD_CMD_DATA_READ, ((I + sector + CachedSectorOffset) * CARD_DATA_BLOCK_SIZE), (CachedPortFlags &~CARD_BLK_SIZE(7)) | CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(1), &buffer[I * 128], CARD_DATA_BLOCK_SIZE);
	}
	return true;
}


bool IO_WriteSectors(u32 sector, u32 numSecs, u32* buffer) {
	if (!hasEnoughSaveData)return fakeWriteSuccess;
	for(u32 I = 0; I < numSecs; I++) {
		bool cachedSectorExists = false;
		int currentBufferTablePosition = 0;
		for (int i = 1; i < bufferTableSize; i++) {
			if (EEPROMBuffer[i] == (sector + I)) {
				currentBufferTablePosition = i;
				cachedSectorExists = true;
				break;
			}
		}
		if (!cachedSectorExists && (bufferTableSize > BufferTableMaxSize)) {
			return false;
		} else if (!cachedSectorExists) {
			bufferTableSize++;
			currentBufferTablePosition = bufferTableSize;
			EEPROMBuffer[currentBufferTablePosition] = (sector + I);
		}
		/*switch (savetype) {
			case 3: {
				cardmePageErase((currentBufferTablePosition + 4) * 256);
				cardmePageErase((currentBufferTablePosition + 5) * 256);
			} break;
		}*/
		cardmeWriteEeprom(((currentBufferTablePosition + 4) * CARD_DATA_BLOCK_SIZE), (u8*)&buffer[I * 128], CARD_DATA_BLOCK_SIZE, savetype);
	}
	/*switch (savetype) {
		case 3: for (int i = 0; i < 8; i++)cardmePageErase(i * 256); break;
	}*/
	cardmeWriteEeprom(0, (u8*)EEPROMBuffer, (CARD_DATA_BLOCK_SIZE * 4), savetype);
	return true;
}


bool IO_IsInserted(void) { return true; }

bool IO_ClearStatus(void) { return true; }

bool IO_Shutdown(void) {
	// Reset volatiles to ensure if driver is copied to new program that it has a consistant startup state.
	hasEnoughSaveData = true;
	savetype = 0;
	savesize = 0;
	bufferTableSize = 0;
	toncset32(cardBuffer, 0xFFFFFFFF, 128);
	toncset32(EEPROMBuffer, 0xFFFFFFFF, 512);
	return true; 
}

