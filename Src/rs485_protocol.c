/*
 * rs485_protocol.c
 *
 *  Created on: 14 ����. 2019 �.
 *      Author: User
 */

#include "rs485_protocol.h"
#include "rs485.h"
#include <string.h>
#include "main.h"
#include "crc.h"
#include "eeprom.h"
#include "os_conf.h"

static uint8_t tmp_rx1_buf[UART_BUF_SISE];
static uint8_t tmp_rx2_buf[UART_BUF_SISE];
static uint8_t tx1_buf[UART_BUF_SISE];
static uint8_t tx2_buf[UART_BUF_SISE];
extern uint16_t VirtAddVarTab[NB_OF_VAR];

uint8_t net_address = 0x01;

#define HOLDR_COUNT 16
#define INPR_COUNT	IREG_CNT + AI_CNT + 5
#define DINPUTS_COUNT	(14 + 14 + 14 + 14) // ����� ����� , ���. ��������� , ����� , ������ ������

#define READ_COILS		1
#define READ_DINPUTS	2
#define READ_HOLD_REGS	3
#define READ_INP_REGS	4
#define WR_SINGLE_COIL	5
#define WR_SINGLE_REG	6
#define WR_MULTI_REGS	0x10
#define WR_MULTI_COIL	0x0F

extern short ain[AI_CNT];
extern short ireg[IREG_CNT];
extern uint16_t ai_type;

extern uint16_t di_state_reg;
extern uint16_t di_sh_circ_reg;
extern uint16_t di_break_reg;
extern uint16_t di_fault_reg;
extern uint8_t din[DI_CNT];
extern uint8_t din_break[DI_CNT];
extern uint8_t din_short_circuit[DI_CNT];
extern uint8_t din_fault[DI_CNT];

static void modbus_error(unsigned char func, unsigned char code, uint8_t * tx_ptr, void (*send)(uint8_t*,uint16_t)) {
	unsigned short crc=0;
	tx_ptr[0] = net_address;
	tx_ptr[1] = func | 0x80;
	tx_ptr[2] = code;
	crc = GetCRC16((unsigned char*)tx_ptr,3);
	tx_ptr[3]=crc>>8;
	tx_ptr[4]=crc&0xFF;
	send(tx_ptr,5);
}

void rx_callback(uint8_t* rx_ptr,uint16_t rx_cnt, uint8_t * tx_ptr, void (*send)(uint8_t*,uint16_t)) {
	uint16_t crc=0;
	uint16_t cnt=0;
	uint16_t mem_addr = 0;
	uint16_t tmp=0;
	uint16_t byte_count = 0;
	if((rx_cnt>=4) && ((rx_ptr[0]==net_address) || (rx_ptr[0]==0x00)) && (GetCRC16(rx_ptr,rx_cnt)==0)) {
		switch(rx_ptr[1]) {
			case 0xA0:
				tx_ptr[0] = rx_ptr[0];
				tx_ptr[1] = 0xA0;
				// request id
				tx_ptr[2] = rx_ptr[2];
				tx_ptr[3] = rx_ptr[3];
				// device id
				tx_ptr[4] = 0x00;
				tx_ptr[5] = 0x00;
				tx_ptr[6] = 0x00;
				tx_ptr[7] = 0x04;

				crc = GetCRC16((unsigned char*)tx_ptr,8);
				tx_ptr[8]=crc>>8;
				tx_ptr[9]=crc&0xFF;
				send(tx_ptr,10);
				break;
			case 0xEB:	// ������� ����� �������� ��� ��������
				tx_ptr[0] = rx_ptr[0];
				tx_ptr[1] = 0xEB;
				// request id
				tx_ptr[2] = rx_ptr[2];
				tx_ptr[3] = rx_ptr[3];
				cnt = rx_ptr[4];
				cnt = cnt<<8; cnt|=rx_ptr[5]; // ������ ��������� � ����������
				if(cnt%128==0) {
					cnt=cnt/128;
					tx_ptr[4] = cnt>>8;
					tx_ptr[5] = cnt & 0xFF;
					crc = GetCRC16((unsigned char*)tx_ptr,6);
					tx_ptr[6]=crc>>8;
					tx_ptr[7]=crc&0xFF;
					send(tx_ptr,8);
				}
				break;
			case 0xEC:// restart in boot mode
				tx_ptr[0] = rx_ptr[0];
				tx_ptr[1] = 0xEC;
				// request id
				tx_ptr[2] = rx_ptr[2];
				tx_ptr[3] = rx_ptr[3];
				crc = GetCRC16((unsigned char*)tx_ptr,4);
				tx_ptr[4]=crc>>8;
				tx_ptr[5]=crc&0xFF;
				send(tx_ptr,6);
				EE_WriteVariable(VirtAddVarTab[1],0);
				HAL_Delay(50);
				NVIC_SystemReset();
				break;
			case READ_DINPUTS:
				mem_addr = ((unsigned short)rx_ptr[2]<<8) | rx_ptr[3];
				cnt = ((unsigned short)rx_ptr[4]<<8) | rx_ptr[5];
				if(cnt>DINPUTS_COUNT || cnt==0) {modbus_error(READ_DINPUTS,0x03,tx_ptr,send);break;}
				if(mem_addr+cnt>DINPUTS_COUNT) {modbus_error(READ_DINPUTS,0x02,tx_ptr,send);break;}
				byte_count = cnt>>3;
				if(cnt!=(byte_count<<3)) byte_count++;
				for(tmp=0;tmp<byte_count;tmp++) tx_ptr[3+tmp] = 0;
				for(tmp=0;tmp<cnt;tmp++) {
					if(mem_addr+tmp<DI_CNT) {if(din[mem_addr+tmp]) tx_ptr[3+(tmp>>3)] |= 1<<(tmp%8);}
					else if(mem_addr+tmp<DI_CNT + DI_CNT) {if(din_short_circuit[mem_addr+tmp-DI_CNT]) tx_ptr[3+(tmp>>3)] |= 1<<(tmp%8);}
					else if(mem_addr+tmp<DI_CNT + DI_CNT + DI_CNT) {if(din_break[mem_addr+tmp-DI_CNT-DI_CNT]) tx_ptr[3+(tmp>>3)] |= 1<<(tmp%8);}
					else if(mem_addr+tmp<DI_CNT + DI_CNT + DI_CNT + DI_CNT) {if(din_fault[mem_addr+tmp-DI_CNT-DI_CNT-DI_CNT]) tx_ptr[3+(tmp>>3)] |= 1<<(tmp%8);}
				}
				tx_ptr[0] = net_address;
				tx_ptr[1] = READ_DINPUTS;
				tx_ptr[2] = byte_count;
				crc = GetCRC16((unsigned char*)tx_ptr,3+byte_count);
				tx_ptr[3+byte_count]=crc>>8;
				tx_ptr[4+byte_count]=crc&0xFF;
				send(tx_ptr,5+byte_count);
				break;
			case READ_INP_REGS:
				mem_addr = ((unsigned short)rx_ptr[2]<<8) | rx_ptr[3];
				cnt = ((unsigned short)rx_ptr[4]<<8) | rx_ptr[5];
				if(cnt>INPR_COUNT || cnt==0) {modbus_error(READ_INP_REGS,0x03,tx_ptr,send);break;}
				if(mem_addr+cnt>INPR_COUNT) {modbus_error(READ_INP_REGS,0x02,tx_ptr,send);break;}

				for(tmp=0;tmp<cnt;tmp++) {
					if(mem_addr+tmp<IREG_CNT) {
						tx_ptr[3+tmp*2] = ireg[mem_addr+tmp]>>8;
						tx_ptr[4+tmp*2] = ireg[mem_addr+tmp]&0xFF;
					}else if(mem_addr+tmp<IREG_CNT + AI_CNT) {
						tx_ptr[3+tmp*2] = ain[mem_addr+tmp-IREG_CNT]>>8;
						tx_ptr[4+tmp*2] = ain[mem_addr+tmp-IREG_CNT]&0xFF;
					}else if(mem_addr+tmp==IREG_CNT + AI_CNT) {
						tx_ptr[3+tmp*2] = ai_type>>8;
						tx_ptr[4+tmp*2] = ai_type&0xFF;
					}else if(mem_addr+tmp==IREG_CNT + AI_CNT + 1) {
						tx_ptr[3+tmp*2] = di_state_reg>>8;
						tx_ptr[4+tmp*2] = di_state_reg&0xFF;
					}else if(mem_addr+tmp==IREG_CNT + AI_CNT + 2) {
						tx_ptr[3+tmp*2] = di_sh_circ_reg>>8;
						tx_ptr[4+tmp*2] = di_sh_circ_reg&0xFF;
					}else if(mem_addr+tmp==IREG_CNT + AI_CNT + 3) {
						tx_ptr[3+tmp*2] = di_break_reg>>8;
						tx_ptr[4+tmp*2] = di_break_reg&0xFF;
					}else if(mem_addr+tmp==IREG_CNT + AI_CNT + 4) {
						tx_ptr[3+tmp*2] = di_fault_reg>>8;
						tx_ptr[4+tmp*2] = di_fault_reg&0xFF;
					}
				}
				tx_ptr[0]=net_address;
				tx_ptr[1]=READ_INP_REGS;
				tx_ptr[2]=cnt*2;
				crc=GetCRC16(tx_ptr,3+cnt*2);
				tx_ptr[3+cnt*2]=crc>>8;
				tx_ptr[4+cnt*2]=crc&0xFF;
				send(tx_ptr,5+cnt*2);
				break;
			case READ_HOLD_REGS:
				mem_addr = ((unsigned short)rx_ptr[2]<<8) | rx_ptr[3];
				cnt = ((unsigned short)rx_ptr[4]<<8) | rx_ptr[5];
				if(cnt>HOLDR_COUNT || cnt==0) {modbus_error(READ_HOLD_REGS,0x03,tx_ptr,send);break;}
				if(mem_addr+cnt>HOLDR_COUNT) {modbus_error(READ_HOLD_REGS,0x02,tx_ptr,send);break;}

				for(tmp=0;tmp<cnt;tmp++) {
					if(mem_addr+tmp==0) {
						tx_ptr[3+tmp*2] = ai_type>>8;
						tx_ptr[4+tmp*2] = ai_type&0xFF;
					}else {
						tx_ptr[3+tmp*2] = 0;
						tx_ptr[4+tmp*2] = 0;
					}
				}
				tx_ptr[0]=net_address;
				tx_ptr[1]=READ_HOLD_REGS;
				tx_ptr[2]=cnt*2;
				crc=GetCRC16(tx_ptr,3+cnt*2);
				tx_ptr[3+cnt*2]=crc>>8;
				tx_ptr[4+cnt*2]=crc&0xFF;
				send(tx_ptr,5+cnt*2);
				break;
			case WR_SINGLE_REG:
				mem_addr = ((unsigned short)rx_ptr[2]<<8) | rx_ptr[3];
				cnt = ((unsigned short)rx_ptr[4]<<8) | rx_ptr[5];
				if(mem_addr >= HOLDR_COUNT) {modbus_error(WR_SINGLE_REG,0x02,tx_ptr,send);break;}
				if(mem_addr==0x00) {
					ai_type = cnt;
				}
				tx_ptr[0]=net_address;
				tx_ptr[1]=WR_SINGLE_REG;
				tx_ptr[2]=mem_addr>>8;
				tx_ptr[3]=mem_addr&0xFF;
				tx_ptr[4]=cnt>>8;
				tx_ptr[5]=cnt&0xFF;
				crc=GetCRC16(tx_ptr,6);
				tx_ptr[6]=crc>>8;
				tx_ptr[7]=crc&0xFF;
				send(tx_ptr,8);
				break;
			case WR_MULTI_REGS:
				mem_addr = ((unsigned short)rx_ptr[2]<<8) | rx_ptr[3];
				cnt = ((unsigned short)rx_ptr[4]<<8) | rx_ptr[5];
				if(cnt>=128 || cnt==0) {modbus_error(WR_MULTI_REGS,0x03,tx_ptr,send);break;}
				if(mem_addr+cnt>HOLDR_COUNT) {modbus_error(WR_MULTI_REGS,0x02,tx_ptr,send);break;}
				for(tmp=0;tmp<cnt;tmp++) {
					if(mem_addr+tmp==0x00) {
						ai_type = rx_ptr[8+tmp*2] | ((unsigned short)rx_ptr[7+tmp*2]<<8);
					}
				}
				tx_ptr[0]=net_address;
				tx_ptr[1]=WR_MULTI_REGS;
				tx_ptr[2]=mem_addr>>8;
				tx_ptr[3]=mem_addr&0xFF;
				tx_ptr[4]=cnt>>8;
				tx_ptr[5]=cnt&0xFF;
				crc=GetCRC16(tx_ptr,6);
				tx_ptr[6]=crc>>8;
				tx_ptr[7]=crc&0xFF;
				send(tx_ptr,8);
				break;
			default:
				tx_ptr[0] = rx_ptr[0];
				tx_ptr[1] = 0xA0;
				// request id
				tx_ptr[2] = rx_ptr[2];
				tx_ptr[3] = rx_ptr[3];
				// device id
				tx_ptr[4] = 0x04;
				tx_ptr[5] = 0x00;
				tx_ptr[6] = 0x00;
				tx_ptr[7] = 0x01;

				crc = GetCRC16((unsigned char*)tx_ptr,8);
				tx_ptr[8]=crc>>8;
				tx_ptr[9]=crc&0xFF;
				send(tx_ptr,10);
		}
	}
}

void rx1_callback(uint8_t* rx_ptr,uint16_t rx_cnt) {
	if(rx_cnt<=UART_BUF_SISE) {
		memcpy(tmp_rx1_buf,rx_ptr,rx_cnt);
		rx_callback(tmp_rx1_buf, rx_cnt, tx1_buf, send_data_to_uart1);
	}
}

void rx2_callback(uint8_t* rx_ptr,uint16_t rx_cnt) {
	if(rx_cnt<=UART_BUF_SISE) {
		memcpy(tmp_rx2_buf,rx_ptr,rx_cnt);
		rx_callback(tmp_rx2_buf, rx_cnt, tx2_buf, send_data_to_uart2);
	}
}
