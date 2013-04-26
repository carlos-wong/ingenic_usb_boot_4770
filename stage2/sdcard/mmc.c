/*
 * Copyright (C) 2009 Ingenic Semiconductor Inc.
 * Author: Taylor <cwjia@ingenic.cn>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "msc.h"
#include "jz4770.h"


/*
  BUS_WIDTH 0  --> 1 BIT 
  BUS_WIDTH 2  --> 4 BIT
*/

#define BUS_WIDTH          0
#define BOOT_FROM_P        0
#define MMC_CLOCK_FAST     24000000

/*
 * External routines
 */
extern void flush_cache_all(void);
extern int serial_init(void);
extern void serial_puts(const char *s);
extern void sdram_init(void);

static int rca;
static int ocr = 0;
static int card_status = 0;
static int highcap= 0;
static int is_sd = 0;
static int capacity=0;

static void wait_prg_done(void)
{
	while (!(REG_MSC_IREG & MSC_IREG_PRG_DONE))
	{
		serial_puts("\nREG_MSC_IREG = ");
		serial_put_hex(REG_MSC_IREG);
	}
		
	REG_MSC_IREG = MSC_IREG_PRG_DONE;	
}

static void wait_tran_done(void)
{
	while (!(REG_MSC_IREG & MSC_IREG_DATA_TRAN_DONE))
		;
	REG_MSC_IREG = MSC_IREG_DATA_TRAN_DONE;	
}

int jz_strcmp(char *s1, char *s2)
{
  while (*s1 != '\0' && *s1 == *s2)
    {
      s1++;
      s2++;
    }

  return (*(unsigned char *) s1) - (*(unsigned char *) s2);
}

/* Stop the MMC clock and wait while it happens */
int jz_mmc_stop_clock(void)
{
	return 0;
	int timeout = 1000000;
	int wait = 336; /* 1 us */ 

	REG_MSC_STRPCL = MSC_STRPCL_CLOCK_CONTROL_STOP;
	while (timeout && (REG_MSC_STAT & MSC_STAT_CLK_EN)) {
		timeout--;
		if (timeout == 0) {
			serial_puts("stop clock timeout\n");
			return -1;
		}
		wait = 336;
		while (wait--)
			;
	}
	return 0;
}

/* Start the MMC clock and operation */
int jz_mmc_start_clock(void)
{
//	REG_MSC_STRPCL = MSC_STRPCL_CLOCK_CONTROL_START | MSC_STRPCL_START_OP;
	REG_MSC_STRPCL = MSC_STRPCL_START_OP;
	return 0;
}

static u8 resp[20];
static void clear_resp()
{
	memset(resp, 0, sizeof(resp));
}


u8 * mmc_cmd(u16 cmd, unsigned int arg, unsigned int cmdat, u16 rtype)
{
	u32 timeout = 0x3fffff;
	int words, i;

//	serial_puts("\ncmd = ");
//	serial_put_hex(cmd);
//	serial_puts("\narg = ");
//	serial_put_hex(arg);
//	serial_puts("\ncmdat = ");
//	serial_put_hex(cmdat);
	clear_resp(); 
	jz_mmc_stop_clock();
	
	REG_MSC_CMD   = cmd;
	REG_MSC_ARG   = arg;
	REG_MSC_CMDAT = cmdat;

	REG_MSC_IMASK = ~MSC_IMASK_END_CMD_RES;
	jz_mmc_start_clock();

	while (timeout-- && !(REG_MSC_STAT & MSC_STAT_END_CMD_RES))
		;
	
	REG_MSC_IREG = MSC_IREG_END_CMD_RES;

	switch (rtype) {
         	case MSC_CMDAT_RESPONSE_R1:
		case MSC_CMDAT_RESPONSE_R3:
	        case MSC_CMDAT_RESPONSE_R6:		
			words = 3;
			break;
		case MSC_CMDAT_RESPONSE_R2:
			words = 8;
			break;
		default:
			return 0;
	}
	for (i = words-1; i >= 0; i--) {
		u16 res_fifo = REG_MSC_RES;
		int offset = i << 1;

		resp[offset] = ((u8 *)&res_fifo)[0];
		resp[offset+1] = ((u8 *)&res_fifo)[1];

	}
	return resp;
}

int mmc_block_readm(u32 src, u32 num, u8 *dst)
{
	u8 *resp;
	u32 stat, timeout, data, cnt, wait, nob;

	resp = mmc_cmd(16, 0x200, 0x1, MSC_CMDAT_RESPONSE_R1);
	REG_MSC_BLKLEN = 0x200;
	REG_MSC_NOB = num / 512;

	if (BUS_WIDTH == 2){
		if (highcap) 
			resp = mmc_cmd(18, src, 0x409, MSC_CMDAT_RESPONSE_R1); // for sdhc card
		else
			resp = mmc_cmd(18, src * 512, 0x409, MSC_CMDAT_RESPONSE_R1);
	}else{
		if (highcap) 
			resp = mmc_cmd(18, src, 0x9, MSC_CMDAT_RESPONSE_R1); // for sdhc card
		else
			resp = mmc_cmd(18, src * 512, 0x9, MSC_CMDAT_RESPONSE_R1);

	}
	nob  = num / 512;

	for (nob; nob >= 1; nob--) {
//		serial_put_hex(nob);
		timeout = 0x7ffffff;
		while (timeout) {
			timeout--;
			stat = REG_MSC_STAT;

			if (stat & MSC_STAT_TIME_OUT_READ) {
				serial_puts("\n MSC_STAT_TIME_OUT_READ\n\n");
				return -1;
			}
			else if (stat & MSC_STAT_CRC_READ_ERROR) {
				serial_puts("\n MSC_STAT_CRC_READ_ERROR\n\n");
				return -1;
			}
			else if (!(stat & MSC_STAT_DATA_FIFO_EMPTY)) {
				/* Ready to read data */
				break;
			}
			wait = 12;
			while (wait--)
				;
		}
		if (!timeout) {
			serial_puts("\n mmc/sd read timeout\n");
			return -1;
		}

		/* Read data from RXFIFO. It could be FULL or PARTIAL FULL */
		cnt = 128;
		while (cnt) {
			while (cnt && (REG_MSC_STAT & MSC_STAT_DATA_FIFO_EMPTY))
				;
			cnt --;
			data = REG_MSC_RXFIFO;
			{
				*dst++ = (u8)(data >> 0);
				*dst++ = (u8)(data >> 8);
				*dst++ = (u8)(data >> 16);
				*dst++ = (u8)(data >> 24);
			}
		}
	}

	while (!(REG_MSC_STAT & MSC_STAT_DATA_TRAN_DONE))
		;

	resp = mmc_cmd(12, 0, 0x41, MSC_CMDAT_RESPONSE_R1);

	REG_MSC_IREG |= MSC_IREG_DATA_TRAN_DONE;
	jz_mmc_stop_clock();

	return 0;
}

int mmc_block_writem(u32 src, u32 num, u8 *dst)
{
	u8 *resp;
	u32 stat, timeout, data, cnt, wait, nob, i, j;
	u32 *wbuf = (u32 *)dst;

	
	resp = mmc_cmd(16, 0x200, 0x1, MSC_CMDAT_RESPONSE_R1);
	REG_MSC_BLKLEN = 0x200;
	REG_MSC_NOB = num / 512;

	if(BUS_WIDTH == 2){
		if (highcap)
			resp = mmc_cmd(25, src, 0x10419 , MSC_CMDAT_RESPONSE_R1); // for sdhc card
		else
			resp = mmc_cmd(25, src * 512, 0x10419 , MSC_CMDAT_RESPONSE_R1);
	}else{
		if (highcap)
			resp = mmc_cmd(25, src, 0x10019 , MSC_CMDAT_RESPONSE_R1); // for sdhc card
		else
			resp = mmc_cmd(25, src * 512, 0x10019 , MSC_CMDAT_RESPONSE_R1);
	}

	nob  = num / 512;
	timeout = 0x3ffffff;


	for (i = 0; i < nob; i++) {
//		serial_put_hex(i);
		timeout = 0x3FFFFFF;
		while (timeout) {
			timeout--;
			stat = REG_MSC_STAT;

			if (stat & (MSC_STAT_CRC_WRITE_ERROR | MSC_STAT_CRC_WRITE_ERROR_NOSTS)){
				serial_puts("CRC WRITE ERROR\n");
				return -1;
			}
			else if (!(stat & MSC_STAT_DATA_FIFO_FULL)) {
				/* Ready to write data */
				break;
			}
//			udelay(1)
			wait = 336;
			while (wait--)
				;
		}
		if (!timeout){
			serial_puts("write timeout\n");
			return -1;
		}

		/* Write data to TXFIFO */
		cnt = 128;
		while (cnt) {
			while(!(REG_MSC_IREG & MSC_IREG_TXFIFO_WR_REQ))
				;
			for (j=0; j<8; j++)
			{	
				REG_MSC_TXFIFO = *wbuf++;
				cnt--;
			}
		}
	}

	while (!(REG_MSC_STAT & MSC_STAT_AUTO_CMD_DONE)) ;
	while (!(REG_MSC_STAT & MSC_STAT_PRG_DONE)) ;

	do{
		resp = mmc_cmd(13, rca, 0x1, MSC_CMDAT_RESPONSE_R1); // for sdhc card
	}while(!(resp[2] & 0x1));   //wait the card is ready for data

	REG_MSC_IREG |= (MSC_IREG_AUTO_CMD_DONE | MSC_IREG_PRG_DONE);
	jz_mmc_stop_clock();
	return 0;
}

static void config_boot_partition(void)
{
	serial_puts("config boot partition\n");
	if(BUS_WIDTH == 2)
		mmc_cmd(6, 0x3b30901, 0x441, MSC_CMDAT_RESPONSE_R1);   /* set boot from partition 1 without ACK*/
	else
		mmc_cmd(6, 0x3b30901, 0x41, MSC_CMDAT_RESPONSE_R1);   /* set boot from partition 1 without ACK*/		
	wait_prg_done();	
	
	if(BUS_WIDTH == 2)
		mmc_cmd(6, 0x3b10101, 0x441, MSC_CMDAT_RESPONSE_R1);   /* set boot bus width -> 4 bit */
	else
		mmc_cmd(6, 0x3b10001, 0x41, MSC_CMDAT_RESPONSE_R1);   /* set boot bus width -> 1 bit */
	wait_prg_done();	
}

static void get_ext_csd(u8 *dst)
{
	u8 *resp;
	u32 stat, timeout, data, cnt, wait, nob;

	REG_MSC_BLKLEN = 0x200;
	REG_MSC_NOB = 1;

	resp = mmc_cmd(8, 0x0, 0x409, MSC_CMDAT_RESPONSE_R1);

	cnt = 128;
	while (cnt) {
		while (cnt && (REG_MSC_STAT & MSC_STAT_DATA_FIFO_EMPTY))
			;
		cnt --;
		data = REG_MSC_RXFIFO;
		{
			*dst++ = (u8)(data >> 0);
			*dst++ = (u8)(data >> 8);
			*dst++ = (u8)(data >> 16);
			*dst++ = (u8)(data >> 24);
		}
	}
}

#if 0
unsigned char buf[2] = {8,1};
static int mmc_unlock(void)
{
	unsigned char *resp;
	unsigned int timeout, stat, wait, j;

	resp = mmc_cmd(16, 0x2, 0x1, MSC_CMDAT_RESPONSE_R1);
	REG_MSC_BLKLEN = 0x2;
	REG_MSC_NOB = 1;

	resp = mmc_cmd(42, 0, 0x419, MSC_CMDAT_RESPONSE_R1);

	timeout = 0x3FFFFFF;
	while (timeout) {
		timeout--;
		stat = REG_MSC_STAT;
		
		if (stat & (MSC_STAT_CRC_WRITE_ERROR | MSC_STAT_CRC_WRITE_ERROR_NOSTS)){
			serial_puts("CRC WRITE ERROR\n");
				return -1;
		}
		else if (!(stat & MSC_STAT_DATA_FIFO_FULL)) {
			/* Ready to write data */
			break;
		}
//			udelay(1)
		wait = 336;
		while (wait--)
			;
	}
	if (!timeout){
		serial_puts("write timeout\n");
		return -1;
	}
	
		/* Write data to TXFIFO */
	while(!(REG_MSC_IREG & MSC_IREG_TXFIFO_WR_REQ))
		;
	REG_MSC_TXFIFO = buf[0];
	REG_MSC_TXFIFO = buf[1];
	sd_mdelay(10);
	return 0;
}
#endif

/*
mmc_erase: the card from src(by sector) to src+num (by sector) will be erased
 */

static int mmc_unpack_r1(u8 *resp)
{
	u32 status = resp[0] | resp[1]<<8 | resp[2]<<16 | resp[3]<<24;

	if (status & R1_OUT_OF_RANGE)
		return MMC_ERROR_OUT_OF_RANGE;
	if (status & R1_ADDRESS_ERROR)
		return MMC_ERROR_ADDRESS;
	if (status & R1_BLOCK_LEN_ERROR)
		return MMC_ERROR_BLOCK_LEN;
	if (status & R1_ERASE_SEQ_ERROR)
		return MMC_ERROR_ERASE_SEQ;
	if (status & R1_ERASE_PARAM)
		return MMC_ERROR_ERASE_PARAM;
	if (status & R1_WP_VIOLATION)
		return MMC_ERROR_WP_VIOLATION;
	if (status & R1_CARD_IS_LOCKED ) 
		return MMC_ERROR_CARD_IS_LOCKED;
	if (status & R1_LOCK_UNLOCK_FAILED)
		return MMC_ERROR_LOCK_UNLOCK_FAILED;
	if (status & R1_COM_CRC_ERROR)
		return MMC_ERROR_COM_CRC;
	if (status & R1_ILLEGAL_COMMAND)
		return MMC_ERROR_ILLEGAL_COMMAND;
	if (status & R1_CARD_ECC_FAILED)
		return MMC_ERROR_CARD_ECC_FAILED;
	if (status & R1_CC_ERROR)
		return MMC_ERROR_CC;
	if (status & R1_ERROR)
		return MMC_ERROR_GENERAL;
	if (status & R1_UNDERRUN)
		return MMC_ERROR_UNDERRUN;
	if (status & R1_OVERRUN)
		return MMC_ERROR_OVERRUN;
	if (status & R1_CID_CSD_OVERWRITE)
		return MMC_ERROR_CID_CSD_OVERWRITE;
	
	return MMC_NO_ERROR;
}


int mmc_erase(u32 start, u32 end, u32 erase_all)
{
	u8 *resp;
	int ret;

	if(erase_all){
		start = 0;
		end = capacity - 1; 
		serial_puts("\nsd_mmc_erase all.\n");
	}

	serial_puts("\nSD erase start:");
	serial_put_hex(start);
	serial_puts("SD erase end:");
	serial_put_hex(end);

	if(start > capacity){
		serial_puts("the addr out of card's capacity\n");
		return MMC_ERROR_OUT_OF_RANGE;
	}

	if(is_sd){

		if (highcap){
			resp = mmc_cmd(32, start, 0x1 , MSC_CMDAT_RESPONSE_R1);
			ret = mmc_unpack_r1(resp+1);
			if(ret){
				serial_puts("set start erase addr error");
				return ret;
			}
			resp = mmc_cmd(33, end, 0x1 , MSC_CMDAT_RESPONSE_R1); 
			ret = mmc_unpack_r1(resp+1);
			if(ret){
				serial_puts("set end erase addr error");
				return ret;
			}
			resp = mmc_cmd(38, 0x0, 0x41 , MSC_CMDAT_RESPONSE_R1);

		}else{
			resp = mmc_cmd(32, start * 512, 0x1 , MSC_CMDAT_RESPONSE_R1);
			ret = mmc_unpack_r1(resp+1);
			if(ret){
				serial_puts("set start erase addr error");
				return ret;
			}
			resp = mmc_cmd(33, end * 512, 0x1 , MSC_CMDAT_RESPONSE_R1);
			if(ret){
				serial_puts("set end erase addr error");
				return ret;
			}

			resp = mmc_cmd(38, 0x0, 0x41 , MSC_CMDAT_RESPONSE_R1);
		}

	}else{

		if (highcap){
			resp = mmc_cmd(35, start, 0x1 , MSC_CMDAT_RESPONSE_R1); // for sdhc card
			ret = mmc_unpack_r1(resp+1);
			if(ret){
				serial_puts("set start erase addr error");
				return ret;
			}
			resp = mmc_cmd(36, end, 0x1 , MSC_CMDAT_RESPONSE_R1); // for sdhc card
			ret = mmc_unpack_r1(resp+1);
			if(ret){
				serial_puts("set end erase addr error\n");
				return ret;
			}

			resp = mmc_cmd(38, 0x0, 0x41 , MSC_CMDAT_RESPONSE_R1); // for sdhc card
		}else{
			resp = mmc_cmd(35, start * 512, 0x1 , MSC_CMDAT_RESPONSE_R1);
			ret = mmc_unpack_r1(resp+1);
			if(ret){
				serial_puts("set start erase addr error");
				return ret;
			}

			resp = mmc_cmd(36, end * 512, 0x1 , MSC_CMDAT_RESPONSE_R1);
			ret = mmc_unpack_r1(resp+1);
			if(ret){
				serial_puts("set end erase addr error");
				return ret;
			}

			resp = mmc_cmd(38, 0x0, 0x41 , MSC_CMDAT_RESPONSE_R1);
		}
	}

	while (!(REG_MSC_STAT & MSC_STAT_PRG_DONE)) ;
	REG_MSC_IREG |= MSC_IREG_PRG_DONE;
	
	resp = mmc_cmd(13, rca, 0x1, MSC_CMDAT_RESPONSE_R1); // for sdhc card
	ret = mmc_unpack_r1(resp+1);
	if(ret){
		serial_puts("mmc erase error");
		serial_put_hex(ret);
		return ret;
	}
	
	
	return 0;
}

static int mmc_found(void)
{
	int retries;
	u8 *resp;
	u8 ext_csd[512];

	serial_puts("MMC card found!\n");
	is_sd = 0;
	resp = mmc_cmd(1, 0x40ff8000, 0x3, MSC_CMDAT_RESPONSE_R3);
//	retries = 1000;
	//while (retries-- && resp && !(resp[4] & 0x80)) {
	while (resp && !(resp[4] & 0x80)) {
		resp = mmc_cmd(1, 0x40ff800000, 0x3, MSC_CMDAT_RESPONSE_R3);
		sd_mdelay(10);
	}
		
	sd_mdelay(10);

	if ( resp[4] & 0x80 ) 
		serial_puts("MMC init ok\n");
	else{ 
		serial_puts("MMC init fail\n");
		return -1;
	}
	
	if(resp[4] & 0x40 )
		highcap = 1;
	else
		highcap =0;

	serial_puts("\nhighcap = ");
	serial_put_hex(highcap);
	/* try to get card id */
	resp = mmc_cmd(2, 0, 0x2, MSC_CMDAT_RESPONSE_R2);
	rca = 0;
	resp = mmc_cmd(3, rca, 0x1, MSC_CMDAT_RESPONSE_R1);

	if(!highcap)
	{
		resp = mmc_cmd(9, rca, 0x2, MSC_CMDAT_RESPONSE_R2);
		u32 c_size= (((resp[8] & 0x3) << 10) | (resp[7] << 2) | (resp[6] >> 6));
		u8 c_size_mult = ((resp[4]&0x0e) >> 1) | resp[3] >> 7;
		u8 read_bl_len = resp[9]&0xf;

		capacity = (c_size + 1) * (1 << (c_size_mult+2)) * (1 << read_bl_len);
		capacity = capacity >> 9;  //by sectors

		serial_puts("capacity:");
		serial_put_hex(capacity);

	}

	REG_MSC_CLKRT = 0;	/* 16/1 MHz */

	resp = mmc_cmd(7, rca, 0x1, MSC_CMDAT_RESPONSE_R1);
#if 1
	if(BUS_WIDTH == 2){
		resp = mmc_cmd(6, 0x3b70101, 0x041, MSC_CMDAT_RESPONSE_R1);
		wait_prg_done();
		serial_puts("set 4 bit bus width\n");
	}

	if(MMC_CLOCK_FAST > 20000000){
		resp = mmc_cmd(6, 0x3b90101, 0x041, MSC_CMDAT_RESPONSE_R1);
		wait_prg_done();
		serial_puts("set high speed\n");
	}
#endif
	if(highcap)
	{
		get_ext_csd(ext_csd);
		u32 sec_count = (ext_csd[212] | ext_csd[213]<<8 | ext_csd[214]<<16 | ext_csd[215]<<24);
		capacity = sec_count;
		serial_puts("capacity:");
		serial_put_hex(capacity);
	}

	if(BOOT_FROM_P)
		config_boot_partition();

	resp = mmc_cmd(13, rca, 0x1, MSC_CMDAT_RESPONSE_R1);

/*
	serial_put_hex(resp[4]);
	serial_put_hex(resp[3]);
	serial_put_hex(resp[2]);
	serial_put_hex(resp[1]);
*/
/*
	if(resp[4])
		mmc_unlock();
*/
	return 0;
}

static int sd_init(void)
{
	int retries, wait;
	u8 *resp;
	u32 c_size, size_mult;
	unsigned int cardaddr;
	serial_puts("SD card found!\n");

	is_sd = 1;
	resp = mmc_cmd(41, 0x40ff8000, 0x3, MSC_CMDAT_RESPONSE_R3);
	retries = 500;
	while (retries-- && resp && !(resp[4] & 0x80)) {
		resp = mmc_cmd(55, 0, 0x1, MSC_CMDAT_RESPONSE_R1);
		resp = mmc_cmd(41, 0x40ff8000, 0x3, MSC_CMDAT_RESPONSE_R3);
		
		sd_mdelay(10);
	}

	if ((resp[4] & 0x80) == 0x80) 
		serial_puts("SD init ok\n");
	else{ 
		serial_puts("SD init fail\n");
		return -1;
	}
	/* try to get card id */
	resp = mmc_cmd(2, 0, 0x2, MSC_CMDAT_RESPONSE_R2);
	resp = mmc_cmd(3, 0, 0x6, MSC_CMDAT_RESPONSE_R1);
	cardaddr = (resp[4] << 8) | resp[3]; 
	rca = cardaddr << 16;

	resp = mmc_cmd(9, rca, 0x2, MSC_CMDAT_RESPONSE_R2);
	highcap = (resp[14] & 0xc0) >> 6;
	if (highcap) {
		c_size = resp[5] | resp[6] << 8 | (resp[7] & 0x3f) << 16;
		capacity = (c_size + 1) << 10;
	} else {
		c_size = (resp[6] & 0xc0) >> 6 | resp[7] << 2 | (resp[8] & 0x3) << 10;
		size_mult = (resp[4] & 0x80) >> 7 | (resp[5] & 0x3) << 1;
		capacity = 2 * ((c_size + 1) << (size_mult + 2));
	}
	serial_puts("capacity:");
	serial_put_hex(capacity);
	
	REG_MSC_CLKRT = 0;
	resp = mmc_cmd(7, rca, 0x41, MSC_CMDAT_RESPONSE_R1);
	resp = mmc_cmd(55, rca, 0x1, MSC_CMDAT_RESPONSE_R1);
	resp = mmc_cmd(6, BUS_WIDTH, 0x1 | (BUS_WIDTH << 9), MSC_CMDAT_RESPONSE_R1);

	return 0;
}

#define PROID_4750 0x1ed0024f

#define read_32bit_cp0_processorid()                            \
({ int __res;                                                   \
        __asm__ __volatile__(                                   \
        "mfc0\t%0,$15\n\t"                                      \
        : "=r" (__res));                                        \
        __res;})


void mmc_init_gpio(void)
{
//#if defined(BOOT_FROM_SD)	
	__gpio_as_msc0_a();
//#elif defined(BOOT_FROM_NAND)
//	__gpio_as_msc0_e();
//#endif
}

/* init mmc/sd card we assume that the card is in the slot */
int  mmc_init(void)
{
	int retries, ret;
	u8 *resp;
	unsigned int cpm_msc_div;

	cpm_msc_div = ((__cpm_get_pllout2()%MMC_CLOCK_FAST) == 0) ?
		(__cpm_get_pllout2()/MMC_CLOCK_FAST) : (__cpm_get_pllout2()/MMC_CLOCK_FAST + 1);

	REG_CPM_MSCCDR(0) = cpm_msc_div - 1;
	REG_CPM_CPCCR |= CPM_CPCCR_CE;

	serial_puts("pll");
	serial_put_hex(__cpm_get_pllout2());
	serial_puts("msccdr");
	serial_put_hex(REG_CPM_MSCCDR(0));

	mmc_init_gpio();

	__msc_reset();

	MMC_IRQ_MASK();	
	REG_MSC_CLKRT = 7;    //187k
	REG_MSC_RDTO =0xffffffff;
	REG_MSC_RESTO = 0xffffffff;
	REG_MSC_LPM = 0x1;
		
//	REG_MSC_STRPCL = 6;
//	while(1);

	if(MMC_CLOCK_FAST >= 48000000)
		REG_MSC_LPM |= 0x1 << 31;

	/* just for reading and writing, suddenly it was reset, and the power of sd card was not broken off */
	resp = mmc_cmd(12, 0, 0x41, MSC_CMDAT_RESPONSE_R1);

	/* reset */
	resp = mmc_cmd(0, 0, 0x80, 0);
	resp = mmc_cmd(8, 0x1aa, 0x1, MSC_CMDAT_RESPONSE_R1);
	resp = mmc_cmd(55, 0, 0x1, MSC_CMDAT_RESPONSE_R1);

	if(resp[5] == 0x37){
		resp = mmc_cmd(41, 0x40ff8000, 0x3, MSC_CMDAT_RESPONSE_R3);
		if(resp[5] == 0x3f){
			ret = sd_init();
		}else{
			ret = mmc_found();
		}
	}else{
		ret = mmc_found();
	}
	return ret;
}

