/*
	cardme by SaTa. for Public Domain
	2005/10/30
		NEW TYPE 2 ASPJ has 512kbit(64Kbyte)EEPROM
	2005/10/21
		NEW TYPE 3 
		cardmeChipErase use cardmeSectorErase
*/

// #include <nds.h>
// #include <string.h>

void cardmeReadEeprom(u32 address, u8 *data, u32 length, u32 addrtype);
int cardmeChipErase(void);	//	USE TO TYPE 3 FLASH MEMORY ONLY

void cardmeSectorErase(u32 address);
void cardmePageErase(u32 address);


int cardmeGetType(void);	//
int cardmeSize(int tp);
u8 cardmeReadID(int i) ;	//	don't work ??
u8 cardmeCMD(u8 cmd,int address) ;

void cardmeReadHeader(uint8 * header);
void cardmeWriteEeprom(u32 address, u8 *data, u32 length, u32 addrtype);

