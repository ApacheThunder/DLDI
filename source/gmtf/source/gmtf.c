/*
	gmtf.c -- DLDI driver for the Datel Game'n'Music card
	
 Copyright (c) 2007 Michael "Chishm" Chisholm
	
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation and/or
     other materials provided with the distribution.
  3. The name of the author may not be used to endorse or promote products derived
     from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


// When compiling for NDS, make sure NDS is defined
#ifndef NDS
 #if defined ARM9 || defined ARM7
  #define NDS
 #endif
#endif

#ifdef NDS
 #include <nds/ndstypes.h>
#else
 #include "gba_types.h"
#endif

#define BYTES_PER_SECTOR 512

#ifndef NULL
 #define NULL 0
#endif

//---------------------------------------------------------------------------------
// The following were taken from libnds
#define	REG_CARD_DATA_RD	(*(vu32*)0x04100010)
#define REG_AUXSPICNT	(*(vu16*)0x040001A0)
#define REG_AUXSPICNTH	(*(vu8*)0x040001A1)
#define REG_AUXSPIDATA	(*(vu8*)0x040001A2)
#define REG_ROMCTRL		(*(vu32*)0x040001A4)
#define REG_CARD_COMMAND	((vu8*)0x040001A8)
#define CARD_CR1_ENABLE  0x80  // in byte 1, i.e. 0x8000
#define CARD_CR1_IRQ     0x40  // in byte 1, i.e. 0x4000
#define CARD_BUSY         (1<<31)           // when reading, still expecting incomming data?
#define CARD_SPI_BUSY		(1<<7)
#define CARD_CR1_EN	0x8000
#define	CARD_CR1_SPI_EN	0x2000
#define	CARD_CR1_SPI_HOLD	0x40
//---------------------------------------------------------------------------------
// ROMCTRL PORTFLAGS
#define CARD_ACTIVATE     (1<<31)           // when writing, get the ball rolling
#define CARD_nRESET       (1<<29)           // value on the /reset pin (1 = high out, not a reset state, 0 = low out = in reset)
#define CARD_SEC_CMD      (1<<22)           // The command transfer will be hardware encrypted (KEY2)
#define CARD_DELAY2(n)    (((n)&0x3F)<<16)  // Transfer delay length part 2
#define CARD_SEC_EN       (1<<14)           // Security enable
#define CARD_SEC_DAT      (1<<13)           // The data transfer will be hardware encrypted (KEY2)
//---------------------------------------------------------------------------------

#define SD_COMMAND_TIMEOUT 0xFFF
#define SD_WRITE_TIMEOUT 0xFFFF
// #define CARD_CR2_SETTINGS 0xA0586000
static const u32 CARD_CR2_SETTINGS = (CARD_ACTIVATE | CARD_nRESET | CARD_SEC_CMD | CARD_DELAY2(0x3F) | CARD_SEC_EN | CARD_SEC_DAT);

#define READ_SINGLE_BLOCK 17
#define WRITE_SINGLE_BLOCK 24
#define SD_WRITE_OK 0x05

#define SPI_START 0xCC
#define SPI_STOP 0xC8

static bool isSdhc = false;
// Clean up CRC
static volatile u32 temp;

static void ndsarSendNtrCommand (const u8 cmd[8], u32 romctrl)
{
    REG_AUXSPICNT = CARD_CR1_EN;

    REG_CARD_COMMAND[0] = cmd[0];
    REG_CARD_COMMAND[1] = cmd[1];
    REG_CARD_COMMAND[2] = cmd[2];
    REG_CARD_COMMAND[3] = cmd[3];
    REG_CARD_COMMAND[4] = cmd[4];
    REG_CARD_COMMAND[5] = cmd[5];
    REG_CARD_COMMAND[6] = cmd[6];
    REG_CARD_COMMAND[7] = cmd[7];
    REG_ROMCTRL = romctrl;
    
    while (REG_ROMCTRL & CARD_BUSY);
}

static void ndsarNtrF2 (u32 param1, u8 param2)
{
    u8 cmd[8] = {0xF2, param1 >> 24, param1 >> 16, param1 >> 8,
                 param1 & 0xFF, param2, 0x00, 0x00};
    
    ndsarSendNtrCommand(cmd, CARD_CR2_SETTINGS);
}

static u8 transferSpiByte (u8 send)
{
	REG_AUXSPIDATA = send;
	while (REG_AUXSPICNT & CARD_SPI_BUSY);
	return REG_AUXSPIDATA;
}

static u8 getSpiByte (void) 
{
	REG_AUXSPIDATA = 0xFF;
	while (REG_AUXSPICNT & CARD_SPI_BUSY);
	return REG_AUXSPIDATA;
}


static void initSpi (u8 cmd)
{
    ndsarNtrF2(0, cmd);

	REG_AUXSPICNT = CARD_CR1_EN | CARD_CR1_SPI_EN | CARD_CR1_SPI_HOLD;
	if (cmd == SPI_STOP)transferSpiByte(0xFF);
}

static u8 getSpiByteTimeout() {
    int timeout = SD_COMMAND_TIMEOUT;
    u8 r1;
    do
    {
        r1 = getSpiByte();
    }
    while (r1 == 0xFF && --timeout > 0);
	return r1;
}

static u8 sendCommandLen (u8 cmdId, u32 arg, void* buff, int len)
{
    u8 cmd[6];

    // Build a SPI SD command to be sent as-is.
    cmd[0] = 0x40 | (cmdId & 0x3f);
    cmd[1] = arg >> 24;
    cmd[2] = arg >> 16;
    cmd[3] = arg >>  8;
    cmd[4] = arg >>  0;
    cmd[5] = (len > 1) ? 0x86 : 0x95; // CRC is mostly ignored in SPI mode.
									  // Because the first CMD0 is not exactly in SPI mode,
									  // this is the valid CRC for CMD8 with 0x1AA argument.

    for(int i = 0; i < sizeof(cmd); i++)
        transferSpiByte(cmd[i]);

    u8 r1 = getSpiByteTimeout();
	
	u8* buff_u8 = (u8*)buff;
	
	for(int i = 0; i < (len - 1); ++i) {
		buff_u8[i] = getSpiByte();
	}

    return r1;
}

static u8 sendCommand(u8 cmdId, u32 arg) {
	return sendCommandLen(cmdId, arg, NULL, 1);
}

static u8 ndsarSdcardCommandSendLen (u8 cmdId, u32 arg, void* buff, int len)
{
    initSpi(SPI_START);
    u8 r1 = sendCommandLen(cmdId, arg, buff, len);
	
    initSpi(SPI_STOP);

    return r1;
}

static u8 ndsarSdcardCommandSend (u8 cmdId, u32 arg)
{
	return ndsarSdcardCommandSendLen(cmdId, arg, NULL, 1);
}

static bool sdRead (u32 sector, u8* dest)
{
	initSpi(SPI_START);
	
	if (sendCommand (READ_SINGLE_BLOCK, sector * (BYTES_PER_SECTOR - (511 * isSdhc))) != 0x00) {
		initSpi(SPI_STOP);
		return false;
	}

	// Wait for data start token
	u8 spiByte = getSpiByteTimeout();

	if (spiByte != 0xFE) {
		initSpi(SPI_STOP);
		return false;
	}
	
	for (int i = BYTES_PER_SECTOR; i > 0; i--) {
		*dest++ = getSpiByte();
	}

	
	temp = getSpiByte();
	temp = getSpiByte();
	
	initSpi(SPI_STOP);
	return true;
}

static bool sdWrite (u32 sector, u8* src)
{
	int i;
	int timeout;
	
	initSpi(SPI_START);
	
	if (sendCommand (WRITE_SINGLE_BLOCK, sector * (BYTES_PER_SECTOR - (511 * isSdhc))) != 0) {
		initSpi(SPI_STOP);
		return false;
	}
	
	// Send start token
	transferSpiByte(0xFE);
	
	// Send data
	for (i = BYTES_PER_SECTOR; i > 0; i--) {
		REG_AUXSPIDATA = *src++;
		while (REG_AUXSPICNT & CARD_SPI_BUSY);
	}

	// Send fake CRC
	transferSpiByte(0xFF);
	transferSpiByte(0xFF);
	
	// Get data response
	if ((getSpiByte() & 0x0F) != SD_WRITE_OK) {
		initSpi(SPI_STOP);
		return false;
	}
	
	// Wait for card to write data
	timeout = SD_WRITE_TIMEOUT;
	while (getSpiByte() == 0 && --timeout > 0);
	
	initSpi(SPI_STOP);
	
	if (timeout == 0) {
		return false;
	}
	
	return true;
}

/*-----------------------------------------------------------------
startUp
Initialize the interface, geting it into an idle, ready state
returns true if successful, otherwise returns false
-----------------------------------------------------------------*/
#define MAX_STARTUP_TRIES 5000

static bool ndsarSdcardInitGnm (void)
{
	bool isv2 = false;
    for (int i = 0; i < 0x100; i++)
	{
        ndsarNtrF2(0x7FFFFFFF | ((i & 1) << 31), 0x00);
	}
    
	ndsarNtrF2(0, SPI_STOP);

    // Send CMD0.
    u8 r1 = ndsarSdcardCommandSend(0, 0);
    if (r1 != 0x01) // Idle State.
    {
        // CMD 0 failed.
        return false;
    }
	
	u32 r7_answer;
	
    r1 = ndsarSdcardCommandSendLen(8, 0x1AA, &r7_answer, 5);
	
	u32 acmd41_arg = 0;
	
	if(r1 == 0x1 && r7_answer == 0xAA010000) {
		isv2 = true;
		acmd41_arg |= (1<<30); // Set HCS bit,Supports SDHC
	}
	
	for(int i = 0; i < MAX_STARTUP_TRIES; ++i) {
		// Send ACMD41.
		ndsarSdcardCommandSend(55, 0);
		r1 = ndsarSdcardCommandSend(41, acmd41_arg);
		if(r1 == 0) {
			break;
		}
	}
	if(r1 != 0)
		return false;
	
	if(isv2) {
		u32 r2_answer;
		r1 = ndsarSdcardCommandSendLen(58, 0, &r2_answer, 5);
		isSdhc = (r2_answer & 0x40) != 0;
	}
	ndsarSdcardCommandSend(16, 0x200);
	
	return true;
}

bool startup(void) {
	return ndsarSdcardInitGnm();
}

/*-----------------------------------------------------------------
isInserted
Is a card inserted?
return true if a card is inserted and usable
-----------------------------------------------------------------*/
bool isInserted (void) {
	return true;
}


/*-----------------------------------------------------------------
clearStatus
Reset the card, clearing any status errors
return  true if the card is idle and ready
-----------------------------------------------------------------*/
bool clearStatus (void) {
	return true;
}


/*-----------------------------------------------------------------
readSectors
Read "numSectors" 512-byte sized sectors from the card into "buffer", 
starting at "sector".
return true if it was successful, false if it failed for any reason
-----------------------------------------------------------------*/
bool readSectors (u32 sector, u32 numSectors, void* buffer) {
	u8* data = (u8*)buffer;
	
	while (numSectors > 0) {
		if (!sdRead (sector, data)) {
			return false;
		}
		sector ++;
		data += BYTES_PER_SECTOR;
		numSectors --;
	}
	
	return true;
}



/*-----------------------------------------------------------------
writeSectors
Write "numSectors" 512-byte sized sectors from "buffer" to the card, 
starting at "sector".
return true if it was successful, false if it failed for any reason
-----------------------------------------------------------------*/
bool writeSectors (u32 sector, u32 numSectors, void* buffer) {
	u8* data = (u8*)buffer;
	
	while (numSectors > 0) {
		if (!sdWrite (sector, data)) {
			return false;
		}
		sector ++;
		data += BYTES_PER_SECTOR;
		numSectors --;
	}
	
	return true;
}

/*-----------------------------------------------------------------
shutdown
shutdown the card, performing any needed cleanup operations
-----------------------------------------------------------------*/
bool shutdown(void) {
	return true;
}

