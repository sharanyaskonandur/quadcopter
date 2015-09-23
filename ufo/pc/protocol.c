/*------------------------------------------------------------
 * Communication protocol
 * 
 * <protocol.c>
 *------------------------------------------------------------
 */

 #include "protocol.h"
 #include <stdio.h>
 //#include <time.h>
 #include "rs232.h"
 #include <stdlib.h>

void add_checksum(struct PACKET* packet) {
	packet->checksum = packet->modecommand ^ (packet->data & 0x000000FF) ^ (packet->data >> 8 & 0x000000FF) ^ (packet->data >> 16 & 0x000000FF) ^ (packet->data >> 24 & 0x000000FF);
	}

	// @deprecated
void generate_pkt(struct PACKET* packet, uint8_t mode, uint8_t command, uint16_t data) {
	packet->header 	= HEADER;
	packet->modecommand = mode; //  <!--- fix this
	// packet->command = command;
	packet->data 	= data;
	add_checksum(packet);
};


uint8_t convert_modecommand(int mode) {
	switch(mode) {
		case SAFE_MODE_INT:
			return SAFE_MODE;
		case PANIC_MODE_INT:
			return PANIC_MODE;
		case MANUAL_MODE_INT:
			return MANUAL_MODE;
		case CALIBRATE_MODE_INT:
			return CALIBRATE_MODE;
		case YAW_CONTROL_INT:
			return YAW_CONTROL;
		case FULL_CONTROL_INT:
			return FULL_CONTROL;
		case 7:
			return SEND_TELEMETRY;
		default:
			return SAFE_MODE;
	}
}

uint32_t convert_data(struct INPUT* inputModel) {
	uint32_t data;

	data = level_convert(inputModel->roll) << 8;
	data = (data | level_convert(inputModel->pitch)) << 8;
	data = (data | level_convert(inputModel->yaw)) << 8;
	data = data | level_convert(inputModel->lift);


	return data;
}

void input_to_pkt(struct INPUT* inputModel, struct PACKET* packet) {
	packet->header = HEADER;
	packet->modecommand = convert_modecommand(inputModel->mode);
	packet->data = convert_data(inputModel);
	add_checksum(packet);
}

//DEBUG purpose
void show_pkt(struct PACKET* packet)
{
	printf("PACKET: {\n");
	printf("	HEADER: %x\n", packet->header);
	printf("	MODECOMMAND: %x\n", packet->modecommand);
	printf("	DATA: %x\n", packet->data);
	printf("	CHECKSUM: %x\n}\n", packet->checksum);
}

uint8_t level_convert(int level)
{
	switch(level){
		case 0:
			return HOVER;
		case 1:
			return LEVEL1;
		case 2:
			return LEVEL2;
		case 3:
			return LEVEL3;
		case 4:
			return LEVEL4;
		case 5:
			return LEVEL5;
		case 6:
			return LEVEL6;
		case 7:
			return LEVEL7;
		case 8:
			return LEVEL8;
		case 9:
			return LEVEL9;
		case 10:
			return LEVEL10;
		case 11:
			return LEVEL11;
		case 12:
			return LEVEL12;
		case 13:
			return LEVEL13;
		case 14:
			return LEVEL14;
		case 15:
			return LEVEL15;

		case -1:
			return LEVEL_N1;
		case -2:
			return LEVEL_N2;
		case -3:
			return LEVEL_N3;
		case -4:
			return LEVEL_N4;
		case -5:
			return LEVEL_N5;
		case -6:
			return LEVEL_N6;
		case -7:
			return LEVEL_N7;
		case -8:
			return LEVEL_N8;
		case -9:
			return LEVEL_N9;
		case -10:
			return LEVEL_N10;
		case -11:
			return LEVEL_N11;
		case -12:
			return LEVEL_N12;
		case -13:
			return LEVEL_N13;
		case -14:
			return LEVEL_N14;
		case -15:
			return LEVEL_N15;
		default :
			return HOVER; 
	}
}

void periodic_send (struct timeval* timer_main, struct timeval* timer_r, struct PACKET* pkt, int link_status)
{
	if ((labs(((timer_main->tv_usec + timer_main->tv_sec*1000000) - (timer_r->tv_usec + timer_r->tv_sec*1000000))) > COMM_T))
	{	
		#ifdef DEBUG
			printf("Sending packet->\n");
		#endif
		if(link_status > -1) {
show_pkt(&pkt);
			rs232_put_pkt(pkt);
		}
		gettimeofday(timer_r, NULL);//reset timer	
	}
}