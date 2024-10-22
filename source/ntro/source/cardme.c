/*
	see:CARDME.H
*/
#ifndef NDS
 #if defined ARM9 || defined ARM7
  #define NDS
 #endif
#endif

#ifdef NDS
 #include <nds.h>
#else
 #include "gba_types.h"
#endif

#include "cardme.h"

#define REG_AUXSPICNT	(*(vu16*)0x040001A0)
#define REG_AUXSPIDATA	(*(vu8*)0x040001A2)

void CardMeEepromWaitBusy() {
	while (REG_AUXSPICNT & /*BUSY*/0x80);
}

#define REG_EXMEMCNT (*(vu16*)0x04000204)

/***********************
void cardmeReadHeader(uint8 * header) {
//---------------------------------------------------------------------------------
	cardRead00(0, (uint32 *)header, 512, 0xA93F1FFF);
}
**********************/

u8 cardmeCMD(u8 cmd,int address) { 
//	int address=i & 1;
	u8 ret;
	int k;
    REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40; 

    REG_AUXSPICNT = 0xFFFF; 
    REG_AUXSPIDATA = cmd;	// 

    CardMeEepromWaitBusy(); 

	REG_AUXSPIDATA = (address >> 16) & 0xFF; 
	CardMeEepromWaitBusy(); 

	REG_AUXSPIDATA = (address >> 8) & 0xFF; 
	CardMeEepromWaitBusy(); 

    REG_AUXSPIDATA = (address) & 0xFF; 
    CardMeEepromWaitBusy(); 

	for(k=0;k<256;k++) {
		ret = REG_AUXSPIDATA; 
		if(ret!=0xFF)break;
		CardMeEepromWaitBusy(); 
	}
    REG_AUXSPICNT = /*MODE*/0x40; 
    return ret;
} 


u8 cardmeReadID(int i) { 
	int address=i & 1;
	u8 ret;
	int k;
    REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40; 

    REG_AUXSPICNT = 0xFFFF; 
    REG_AUXSPIDATA = 0xAB;	// READ ID

    CardMeEepromWaitBusy(); 

	REG_AUXSPIDATA = (address >> 16) & 0xFF; 
	CardMeEepromWaitBusy(); 

	REG_AUXSPIDATA = (address >> 8) & 0xFF; 
    CardMeEepromWaitBusy(); 

    REG_AUXSPIDATA = (address) & 0xFF; 
    CardMeEepromWaitBusy();

	for(k=0;k<16;k++) {
		ret = REG_AUXSPIDATA; 
		if(ret!=0xFF)break;
		CardMeEepromWaitBusy(); 
	}
    REG_AUXSPICNT = /*MODE*/0x40; 
    return ret;
} 

//////////////////////////////////////////////////////////////////////
void cardmeReadEeprom(u32 address, u8 *data, u32 length, u32 addrtype) { 
    REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40; 
//    REG_AUXSPICNT = 0xFFFF; 
    REG_AUXSPIDATA = 0x03 | ((addrtype == 1) ? address>>8<<3 : 0);
    CardMeEepromWaitBusy(); 

    if (addrtype == 3) { 
		REG_AUXSPIDATA = (address >> 16) & 0xFF; 
		CardMeEepromWaitBusy(); 
    } 
    if (addrtype >= 2) { 
		REG_AUXSPIDATA = (address >> 8) & 0xFF; 
		CardMeEepromWaitBusy(); 
    } 
    REG_AUXSPIDATA = (address) & 0xFF; 
    CardMeEepromWaitBusy(); 

    while (length > 0) { 
		REG_AUXSPIDATA = 0; 
		CardMeEepromWaitBusy(); 
		*data++ = REG_AUXSPIDATA; 
		length--; 
    } 
	CardMeEepromWaitBusy(); 
    REG_AUXSPICNT = /*MODE*/0x40; 
} 
//////////////////////////////////////////////////////////////////////


#define T1BLOCKSZ (16)
//  TYPE1 4kbit EEPROM
void cardmeWriteEepromT1(u32 address, u8 *apData, u32 aLength, u32 addrtype) {
	int i;
	u32 last=address+aLength;

	while (address < last) {
		// set WEL (Write Enable Latch)
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x06; 
        CardMeEepromWaitBusy();

		REG_AUXSPICNT = /*MODE*/0x40;

		// program maximum of 16 bytes
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		//	WRITE COMMAND 0x02 + A8 << 3
		REG_AUXSPIDATA = 0x02 | (address & BIT(8)) >> (8-3) ; 
        CardMeEepromWaitBusy();
		REG_AUXSPIDATA = address & 0xFF; 
        CardMeEepromWaitBusy();


		for (i = 0; address < last && i < T1BLOCKSZ; i++, address++) { 
            REG_AUXSPIDATA = *apData; 
            CardMeEepromWaitBusy(); 
            apData++;
        }

		REG_AUXSPICNT = /*MODE*/0x40;

		// wait programming to finish
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x05; 
        CardMeEepromWaitBusy();

		do { 
			REG_AUXSPIDATA = 0; 
			CardMeEepromWaitBusy(); 
        } while (REG_AUXSPIDATA & 0x01);	// WIP (Write In Progress) ?
        CardMeEepromWaitBusy(); 
		REG_AUXSPICNT = /*MODE*/0x40;
	}
}


#define T2BLOCKSZ (32)
//  TYPE2 64kbit EEPROM
void cardmeWriteEepromT2(u32 address, u8 *apData, u32 aLength, u32 addrtype) {
	int i;
	u32 last=address+aLength;

	while (address < last) {
		// set WEL (Write Enable Latch)
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x06; 
        CardMeEepromWaitBusy();

		REG_AUXSPICNT = /*MODE*/0x40;

		// program maximum of 32 bytes
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x02; 
        CardMeEepromWaitBusy();
        REG_AUXSPIDATA = address >> 8; 
        CardMeEepromWaitBusy(); 
		REG_AUXSPIDATA = address & 0xFF; 
        CardMeEepromWaitBusy();


		for (i = 0; address < last && i < T2BLOCKSZ; i++, address++) { 
			REG_AUXSPIDATA = *apData; 
			CardMeEepromWaitBusy(); 
			apData++;
        }

		REG_AUXSPICNT = /*MODE*/0x40;

		// wait programming to finish
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x05; 
        CardMeEepromWaitBusy();

		do {
			REG_AUXSPIDATA = 0; 
			CardMeEepromWaitBusy(); 
        } while (REG_AUXSPIDATA & 0x01);	// WIP (Write In Progress) ?
        CardMeEepromWaitBusy(); 
		REG_AUXSPICNT = /*MODE*/0x40;
	}
}


#define T3BLOCKSZ (256)
//  TYPE3 2Mbit FLASH MEMORY
void cardmeWriteEepromT3(u32 address, u8 *apData, u32 aLength, u32 addrtype) {
	int i;
	u32 last=address+aLength;

	while (address < last) {
		// set WEL (Write Enable Latch)
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x06; 
        CardMeEepromWaitBusy();

		REG_AUXSPICNT = /*MODE*/0x40;

		// program maximum of 32 bytes
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x02; 
        CardMeEepromWaitBusy();
        REG_AUXSPIDATA = (address >> 16) & 0xFF; 
        CardMeEepromWaitBusy(); 
        REG_AUXSPIDATA = (address >> 8) & 0xFF; 
        CardMeEepromWaitBusy(); 
		REG_AUXSPIDATA = address & 0xFF; 
        CardMeEepromWaitBusy();


		for (i = 0; address < last && i < T3BLOCKSZ; i++, address++) {
			REG_AUXSPIDATA = *apData; 
			CardMeEepromWaitBusy(); 
			apData++;
        }

		REG_AUXSPICNT = /*MODE*/0x40;

		// wait programming to finish
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x05; 
        CardMeEepromWaitBusy();

		do {
			REG_AUXSPIDATA = 0; 
			CardMeEepromWaitBusy(); 
        } while (REG_AUXSPIDATA & 0x01);	// WIP (Write In Progress) ?
        CardMeEepromWaitBusy(); 
		REG_AUXSPICNT = /*MODE*/0x40;
	}

}

//	COMMAND Sec.erase 0xD8 
void cardmeSectorErase(u32 address) {
		// int i;
		// set WEL (Write Enable Latch)
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x06; 
        CardMeEepromWaitBusy();

		REG_AUXSPICNT = /*MODE*/0x40;

		// SectorErase 0xD8
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0xD8;
        CardMeEepromWaitBusy();
        REG_AUXSPIDATA = (address >> 16) & 0xFF; 
        CardMeEepromWaitBusy(); 
        REG_AUXSPIDATA = (address >> 8) & 0xFF; 
        CardMeEepromWaitBusy(); 
		REG_AUXSPIDATA = address & 0xFF; 
        CardMeEepromWaitBusy();

		REG_AUXSPICNT = /*MODE*/0x40;

		// wait erase to finish
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x05;
        CardMeEepromWaitBusy();

		do {
			REG_AUXSPIDATA = 0; 
			CardMeEepromWaitBusy(); 
        } while (REG_AUXSPIDATA & 0x01);	// WIP (Write In Progress) ?
//        CardMeEepromWaitBusy(); 
		REG_AUXSPICNT = /*MODE*/0x40;
}

//	COMMAND Page.erase 0x81 // Page erase for 0x100 (256 byte per page) erase.
// More useful in this specific application as the larger 0xD8 sector erase command erases in 0x10000 blocks which is too large for a DLDI driver properly maintain.
void cardmePageErase(u32 address) {
		// int i;
		// set WEL (Write Enable Latch)
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x06; 
        CardMeEepromWaitBusy();

		REG_AUXSPICNT = /*MODE*/0x40;

		// SectorErase 0xD8
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x81;
        CardMeEepromWaitBusy();
        REG_AUXSPIDATA = (address >> 16) & 0xFF; 
        CardMeEepromWaitBusy(); 
        REG_AUXSPIDATA = (address >> 8) & 0xFF; 
        CardMeEepromWaitBusy(); 
		REG_AUXSPIDATA = address & 0xFF; 
        CardMeEepromWaitBusy();

		REG_AUXSPICNT = /*MODE*/0x40;

		// wait erase to finish
		REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
		REG_AUXSPIDATA = 0x05;
        CardMeEepromWaitBusy();

		do {
			REG_AUXSPIDATA = 0; 
			CardMeEepromWaitBusy(); 
        } while (REG_AUXSPIDATA & 0x01);	// WIP (Write In Progress) ?
//        CardMeEepromWaitBusy(); 
		REG_AUXSPICNT = /*MODE*/0x40;
}

//	Chip Erase  : clear FLASH MEMORY (TYPE 3 ONLY)
//	COMMAND 0xC7
int cardmeChipErase_old(void) {
	// {
	// set WEL (Write Enable Latch)
	REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
	REG_AUXSPIDATA = 0x06; 
    CardMeEepromWaitBusy();

	REG_AUXSPICNT = /*MODE*/0x40;

	// Chip Erase 0xC7
	REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
	REG_AUXSPIDATA = 0xC7; 
    CardMeEepromWaitBusy();

	REG_AUXSPICNT = /*MODE*/0x40;

	// wait programming to finish
	REG_AUXSPICNT = /*E*/0x8000 | /*SEL*/0x2000 | /*MODE*/0x40;
	REG_AUXSPIDATA = 0x05; 
    CardMeEepromWaitBusy();

	do {
		REG_AUXSPIDATA = 0; 
		CardMeEepromWaitBusy(); 
    } while (REG_AUXSPIDATA & 0x01);	// WIP (Write In Progress) ?
    CardMeEepromWaitBusy(); 
	REG_AUXSPICNT = /*MODE*/0x40;

	// }
	return 0;
}


void cardmeWriteEeprom(u32 address, u8 *apData, u32 aLength, u32 addrtype) {
	switch(addrtype) {
		case 1: {
			cardmeWriteEepromT1(address,apData,aLength,addrtype);
			return;
		}break;
		case 2: {
			cardmeWriteEepromT2(address,apData,aLength,addrtype);
			return;
		}break;
		case 3: {
			cardmeWriteEepromT3(address,apData,aLength,addrtype);
			return;
		}break;
	}
}

//////////////////////////////////////////////////////////////////////

int cardmeSizeT3(void) {
	u8 c9f;
	c9f=cardmeCMD(0x9f,0);

	if(c9f==0x17) { //	NEW TYPE 3+ 64Mbit(8192MByte)
		return 8192*1024;	//	 NEW TYPE 3+ 8Mbit(1024KByte)
	}
	if(c9f==0x14) { //	NEW TYPE 3+ 8Mbit(1024MByte)
		return 1024*1024;	//	 NEW TYPE 3+ 8Mbit(1024KByte)
	}
	if(c9f==0x13) {	//	NEW TYPE 3+ 4Mbit(512MByte)
		return 512*1024;	//	 NEW TYPE 3+ 4Mbit(512KByte)
	}
	return 256*1024;		//	TYPE 3  2Mbit(256KByte)
}


#define OFOT0 (8*1024-1)	//	8KBの末端
#define OFOT1 (2*8*1024-1)	//	16KBの末端
int cardmeSizeT2(void) {
	int tp=2;

	u8 buf1[1];	//	+0k data	read -> write
	u8 buf2[1];	//	+8k data	read -> read
	u8 buf3[1];	//	+0k ~data	   write
	u8 buf4[1];	//	+8k data new	comp buf2

	cardmeReadEeprom(OFOT0,buf1,1,tp);
	cardmeReadEeprom(OFOT1,buf2,1,tp);
	buf3[0]=~buf2[0];

	cardmeWriteEeprom(OFOT0,buf3,1,tp);
	cardmeReadEeprom(OFOT0,buf4,1,tp);
	cardmeWriteEeprom(OFOT0,buf1,1,tp);
	if(buf4[0]!=buf3[0])return 0; // Not type 2

	cardmeReadEeprom(OFOT1,buf4,1,tp);
	
	//	+8kも書き換わっている
	if(buf4[0]!=buf2[0])return 8*1024;	//	 8KB(64kbit)
	
	return 64*1024;		//	64KB(512kbit)
}


//	Chip Erase NEW : clear FLASH MEMORY (TYPE 3 ONLY)
int cardmeChipErase(void) {
	int sz;
	void cardmeSectorErase(u32 address);
	sz= cardmeSizeT3();
	
	cardmeSectorErase(0x00000);
	cardmeSectorErase(0x10000);
	cardmeSectorErase(0x20000);
	cardmeSectorErase(0x30000);
	if(sz==512*1024) {
		cardmeSectorErase(0x40000);
		cardmeSectorErase(0x50000);
		cardmeSectorErase(0x60000);
		cardmeSectorErase(0x70000);
	}
	if(sz==1024*1024) {
		cardmeSectorErase(0x80000);
		cardmeSectorErase(0x90000);
		cardmeSectorErase(0xA0000);
		cardmeSectorErase(0xB0000);
		cardmeSectorErase(0xC0000);
		cardmeSectorErase(0xD0000);
		cardmeSectorErase(0xE0000);
		cardmeSectorErase(0xF0000);
	}
	return 0;
}


// extern	int cardmeSizeT2(void);

int cardmeGetType(void) {
//	u8 ret;
	u8 c00;
	u8 c05;
	u8 c9f;
	// u8 c03;
	
	REG_EXMEMCNT &= ~0x0880;	// DS Card access ARM9:bit11=0   GBA Cart access ARM9:bit7=0
	// c03=cardmeCMD(0x03,0);
	cardmeCMD(0x03,0);
	c05=cardmeCMD(0x05,0);
	c9f=cardmeCMD(0x9f,0);
	c00=cardmeCMD(0x00,0);
	// if((c00==0x00) && (c9f==0x00))return 0;	//	PassMe? 判別不可
	if((c00==0x00) && (c9f==0x00))return 1;	//	PassMe? 判別不可
	// if((c00==0xff) && (c05==0xff) && (c9f==0xff))return -1;

	if((c00==0xff) &&  (c05 & 0xFD) == 0xF0 && (c9f==0xff))return 1;
	if((c00==0xff) &&  (c05 & 0xFD) == 0x00 && (c9f==0xff))return 2;
	if((c00==0xff) &&  (c05 & 0xFD) == 0x00 && (c9f==0x00))return 3;
	if((c00==0xff) &&  (c05 & 0xFD) == 0x00 && (c9f==0x12))return 3;	//	NEW TYPE 3
	if((c00==0xff) &&  (c05 & 0xFD) == 0x00 && (c9f==0x13))return 3;	//	NEW TYPE 3+ 4Mbit
	if((c00==0xff) &&  (c05 & 0xFD) == 0x00 && (c9f==0x14))return 3;	//	NEW TYPE 3+ 8Mbit
	if((c00==0xff) &&  (c05 & 0xFD) == 0x00 && (c9f==0x16))return 3;	//	NEW TYPE 3+ 32Mbit
	if((c00==0xff) &&  (c05 & 0xFD) == 0x84 && (c9f==0x17))return 3;	//	NEW TYPE 3+ 64Mbit

	//	/* 判定不可 */
	if(cardmeSizeT2() != 0)return 2;
	// return 0;
	return 1;
}


int cardmeSize(int tp) {
	if(-1 <= tp && tp <= 3) {
		//            -1    0    1    2         3
//		int eepsz[5]={ 0,8192, 512,	8192,		256*1024};
		int eepsz[5]={ 0,8192, 512,	64*1024,	256*1024};

		if((tp==2) || (tp==0))return cardmeSizeT2();
		
		if(tp==3)return cardmeSizeT3();
		tp++;
		return eepsz[tp];
	}
	// return 0;
	return 512;
}

