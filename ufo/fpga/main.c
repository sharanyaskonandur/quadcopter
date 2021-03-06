/*
 *------------------------------------------------------------------
 *  QR
 *------------------------------------------------------------------
 */
#include <stdio.h>
#include <x32.h>
#include <stdint.h>
#include <stdlib.h>

#include "log.h"
#include "defines.h"
#include "assert.h"

#include "../modules/pkt/pkt.h"
#include "../modules/pkt/pkt_checksum.h"

#include "../modules/fp/fp.h"
#include "../modules/fp/fp_conversion.h"
#include "../modules/fp/fp_arithmetic.h"

// RS232 input buffers
uint8_t	fifo[FIFOSIZE]; 
int	iptr = 0; 
int optr = 0;
char c;
int communication_lost = FALSE;

// 
struct LOG log[LOG_LENGTH];
int	ALIVE = 1;
int	ae[4];

// State variables
int mode = SAFE_MODE_INT;
int calibrated = FALSE;
int YAW_CONTROL_LOOP = FALSE;
int FULL_CONTROL_LOOP = FALSE;

int	sax, say, saz, sp, sq, sr, timestamp;
int	sax0, say0, saz0, sp0, sq0, sr0; //Callibration offsets

// X32 variables
int	isr_qr_counter;
int	isr_qr_time;
int	button;
int	inst;

// Logging
int log_sent = 0;
int log_counter = 0;

int lift_setpoint = 0;
int lift_setpoint_rpm = 0;

// filter & yaw control
float_x32 zr_filtered_old = 0x0; // 0
float_x32 zr_old = 0x0; // = 0
float_x32 a0 = 0x4000; // = 1
float_x32 a1 = 0x1f3;  // = 0.0305;
float_x32 b1 = 0x1f3;  // = 0.0305;
float_x32 zr_v;
float_x32 p2phi = 0x43; // = 0.00410
int zr_filtered = 0; // = rounded(zr_filtered_old) = 0

// Full Control constants
float_x32 C1 = 0x400000; 
float_x32 C2 = 0x3d0000;
float_x32 C1_inv = 0x40; 
float_x32 C2_inv = 0x1; 
float_x32 p2phi_inv = 0x3d0000;

int full_yaw = 0;
int full_pitch = 0;
int full_roll = 0;
int yaw = 0;
int pitch = 0;
int roll = 0;

// phi variable
float_x32 p_kalman, phi_kalman, phi_error, p_bias, sp_old, p_bias_old, phi_kalman_old;
// theta variables 
float_x32 q_kalman, theta_kalman,theta_error, q_bias,sq_old,q_bias_old, theta_kalman_old;

// P Values + sensitivity
uint8_t yaw_p = 1;
uint8_t full_p1 = 1;
uint8_t full_p2 = 1;
uint8_t sensitivity = 30;
int lift_step, yaw_step, other_step;

// Start packet protocol variables
uint8_t modecommand;
uint8_t data1;
uint8_t data2;
uint8_t data3;
uint8_t data4;
uint8_t checksum;
uint8_t checker;
// End packet

uint8_t data2_old = 0;
uint8_t data3_old = 0;
uint8_t data4_old = 0;

int startTimestamp, endTimestamp, counter;

// Define functions in main
void 	delay_ms(int ms);
void 	delay_us(int us);
void 	epileptic_delay_ms(int ms);
void 	toggle_led(int i);
void 	reset_leds(void);
void 	print_state(void);

void 	calibrate(void);
int 	zax(void)	{return sax - sax0;}
int 	zay(void)	{return say - say0;}
int 	zaz(void)	{return saz - saz0;}
int 	zp(void)	{return sp - sp0;}
int 	zq(void)	{return sq - sq0;}
int 	zr(void)	{return sr - sr0;}

void 	periodic(void);
void 	isr_qr_link(void);
void 	isr_rs232_rx(void);

void     initiliaze_kalman_filter();

void	move_optr();
int 	get_packet(void);
void	process_packet(void);

// QR behaviour
void 	panic();
void	logs_send();

int within_bounds(int x, int lower_limit, int upper_limit);
int process_data_field (uint8_t* data, uint8_t* data_old, int* knob);
void updateControlModifiers();

/*------------------------------------------------------------------
 * main loop
 *------------------------------------------------------------------
 */

int main() 
{
	// Initialize timestamp variables
	int timestamp_alive_led_toggle = 0;
	int timestamp_last_pkt = 0;
	int count = 1;
	
	// Initialize sensor readings
	sax = say = saz = sp = sq = sr = 0;

	/* prepare QR rx interrupt handler
	*/
	SET_INTERRUPT_VECTOR(INTERRUPT_XUFO, &isr_qr_link);
	SET_INTERRUPT_PRIORITY(INTERRUPT_XUFO, 24);
	isr_qr_counter = isr_qr_time = 0;
	ae[0] = ae[1] = ae[2] = ae[3] = 0;
	ENABLE_INTERRUPT(INTERRUPT_XUFO);

	/* prepare timer interrupt #1
	*/
	X32_timer_per = 1 * CLOCKS_PER_MS;
	SET_INTERRUPT_VECTOR(INTERRUPT_TIMER1, &periodic);
	SET_INTERRUPT_PRIORITY(INTERRUPT_TIMER1, 22);
	ENABLE_INTERRUPT(INTERRUPT_TIMER1);

	/* prepare rs232 rx interrupt and getchar handler
	*/
	SET_INTERRUPT_VECTOR(INTERRUPT_PRIMARY_RX, &isr_rs232_rx);
	SET_INTERRUPT_PRIORITY(INTERRUPT_PRIMARY_RX, 20);
	while (X32_rs232_char) c = X32_rs232_data; // empty buffer
	ENABLE_INTERRUPT(INTERRUPT_PRIMARY_RX);

	/* initialize some other stuff
	*/
	reset_leds();
	initiliaze_kalman_filter();
	updateControlModifiers();

	ENABLE_INTERRUPT(INTERRUPT_GLOBAL); 

	while (ALIVE)
	{
		//startTimestamp = X32_us_clock; //time measurements
		c=get_packet();  //<- possibly add no change packet
		if (c != -1) {
			process_packet();
			timestamp_last_pkt = X32_ms_clock;
		}

		// Toogle alive led
	 	if(X32_ms_clock - timestamp_alive_led_toggle > 1000) {
			toggle_led(0);
			timestamp_alive_led_toggle = X32_ms_clock;
		}  

		// we have lost communication to the qr -> panic
		if ((X32_ms_clock - timestamp_last_pkt) > THRESHOLD && timestamp_last_pkt != 0) {	
			panic();
			YAW_CONTROL_LOOP = FALSE;
			FULL_CONTROL_LOOP = FALSE;
			communication_lost = TRUE;
			timestamp_last_pkt = 0;
		}
		PRINT_STATE(250);
		//endTimestamp = X32_us_clock - startTimestamp;
	}

	// If for some reason the qr is not in safe/panic mode -> panic 
	if(mode != PANIC_MODE_INT || mode != SAFE_MODE_INT) {
		panic();
	} 

	printf("Exit\r\n");
    DISABLE_INTERRUPT(INTERRUPT_GLOBAL);
	return 0;
}

void process_packet(void)  //we need to process packet and decide what should be done
{
	if (mode == PANIC_MODE_INT){
		return;	
	}

	DISABLE_INTERRUPT(INTERRUPT_GLOBAL);
	if ((modecommand == SAFE_MODE) )
		{
			SET_ALL_ENGINE_RPM(0);
			YAW_CONTROL_LOOP = FALSE;
			FULL_CONTROL_LOOP = FALSE;
			mode = SAFE_MODE_INT;
			communication_lost = FALSE; //it's safe now, resume	
		}
	else if (communication_lost)
		{
			printf("$Communication failure: switch to SAFE MODE now!\n");
			ENABLE_INTERRUPT(INTERRUPT_GLOBAL);
			return; //do nothing until term switches to safe mode
		}
	else if ( (modecommand == PANIC_MODE) && (mode != SAFE_MODE_INT))
		{
			YAW_CONTROL_LOOP = FALSE;
			FULL_CONTROL_LOOP = FALSE;
			panic();
			DISABLE_INTERRUPT(INTERRUPT_GLOBAL);
		}
	else if ((modecommand == YAW_CONTROL) && (calibrated == TRUE))
		{
			mode = YAW_CONTROL_INT;
			if ( (data1&0x10) == 0x00) //LIFT
				{
					//modify engine values if lift is changed!
					//DND the yaw control otherwise
					if (lift_setpoint != (int)data1&0x0F)
					{
						lift_setpoint = (int)(data1&0x0F);
						lift_setpoint_rpm = lift_setpoint * 65;
						SET_ALL_ENGINE_RPM(lift_setpoint_rpm);
						if (lift_setpoint > 4) YAW_CONTROL_LOOP = TRUE;
						else YAW_CONTROL_LOOP = FALSE;
					}
				}
			process_data_field (&data2, &data2_old, &yaw);
		}
	else if ((modecommand == FULL_CONTROL) && (calibrated == TRUE))
		{
			mode = FULL_CONTROL_INT;
			if ( (data1&0x10) == 0x00) //LIFT
				{
					if (lift_setpoint != (int)data1&0x0F)
					{
						lift_setpoint = (int)(data1&0x0F);
						lift_setpoint_rpm = lift_setpoint * 65;
						SET_ALL_ENGINE_RPM(lift_setpoint_rpm);
						if (lift_setpoint > 4) 	FULL_CONTROL_LOOP = TRUE;
						else FULL_CONTROL_LOOP = FALSE;
					}
				}
			process_data_field (&data2, &data2_old, &yaw);
			process_data_field (&data3, &data3_old, &pitch);
			process_data_field (&data4, &data4_old, &roll);		
		}
	else if ((modecommand == FULL_CONTROL) && (calibrated == FALSE))
		{
			printf("$QR must be calibrated first! \n");
		}
	else if ((modecommand == YAW_CONTROL) && (calibrated == FALSE))
		{
			printf("$QR must be calibrated first! \n");
		}
	else if (modecommand == MANUAL_MODE)
		{
			mode = MANUAL_MODE_INT;
			//LIFT
			if ( (data1&0x10) == 0x00)
			{
				SET_ALL_ENGINE_RPM(MIN(lift_step * (data1&0x0F),MAXIMUM_ENGINE_SPEED));
			} // Ignoring negative values for lift

			if (ae[0] >= CONTROLS_THRESHOLD) //allow other controls if engines are at sufficient rpm
			{
				//ROLL
				if ( (data4&0x10) == 0x00) 
				{
					//lean left
					ae[1] = MIN(ae[1] + other_step * (data4&0x0F), MAXIMUM_ENGINE_SPEED);
					ae[3] = MAX(ae[3] - other_step * (data4&0x0F), MINIMUM_ENGINE_SPEED);
				}
				else
				{
					//lean right
					ae[3] = MIN(ae[3] + other_step * (data4&0x0F), MAXIMUM_ENGINE_SPEED);
					ae[1] = MAX(ae[1] - other_step * (data4&0x0F), MINIMUM_ENGINE_SPEED);
				}

				//PITCH
				if ( (data3&0x10) == 0x00) 
				{
					//lean forward
					ae[2] = MIN(ae[2] + other_step * (data3&0x0F), MAXIMUM_ENGINE_SPEED); 
					ae[0] = MAX(ae[0] - other_step * (data3&0x0F), MINIMUM_ENGINE_SPEED); 
				}
				else
				{
					 //lean backward
					ae[0] = MIN(ae[0] + other_step * (data3&0x0F), MAXIMUM_ENGINE_SPEED);
					ae[2] = MAX(ae[2] - other_step * (data3&0x0F), MINIMUM_ENGINE_SPEED); 
				}

				//YAW
				if ( (data2&0x10) == 0x00) 
				{
					// Left
					ae[0] = MIN(ae[0] + yaw_step * (data2&0x0F), MAXIMUM_ENGINE_SPEED);
					ae[2] = MIN(ae[2] + yaw_step * (data2&0x0F), MAXIMUM_ENGINE_SPEED);
					ae[1] = MAX(ae[1] - yaw_step * (data2&0x0F), MINIMUM_ENGINE_SPEED);
					ae[3] = MAX(ae[3] - yaw_step * (data2&0x0F), MINIMUM_ENGINE_SPEED);
				}
				else
				{
					// Right
					ae[1] = MIN(ae[1] + yaw_step * (data2&0x0F), MAXIMUM_ENGINE_SPEED);
					ae[3] = MIN(ae[3] + yaw_step * (data2&0x0F), MAXIMUM_ENGINE_SPEED);
					ae[0] = MAX(ae[0] - yaw_step * (data2&0x0F), MINIMUM_ENGINE_SPEED);
					ae[2] = MAX(ae[2] - yaw_step * (data2&0x0F), MINIMUM_ENGINE_SPEED);
		 		}
			}
		}
		else if ( (modecommand == SEND_TELEMETRY) && (mode == SAFE_MODE_INT) && !log_sent ) 
		{
			logs_send();
		}
		else if ( (modecommand == CALIBRATE_MODE) && (mode == SAFE_MODE_INT) )
		{
			delay_ms(1000);
			calibrate();
			printf("$QR calibrated...\n");
			toggle_led(3);
			delay_ms(500);
			toggle_led(3);
			delay_ms(500);
			toggle_led(3);

			calibrated = TRUE;
		}
		else if (modecommand == P_VALUES_MODE)  
		{
			yaw_p 	= within_bounds(data1,1,20);
			full_p1 = within_bounds(data2,1,20);
			full_p2 = within_bounds(data3,1,20);
			sensitivity = within_bounds(data4,0,60);
			updateControlModifiers();			
		}
	ENABLE_INTERRUPT(INTERRUPT_GLOBAL);
}

void logs_send() {
	int i = 0;
	#ifdef LOGGING
		printf("********SENDING LOG DATA!**********\n");
		for (i=0; i < log_counter; i++)
		{
			printf("%d ", log[i].timestamp );
			printf("%d ", log[i].ae[0] );
			printf("%d ", log[i].ae[1] );
			printf("%d ", log[i].ae[2] );
			printf("%d ", log[i].ae[3] );
			printf("%d ", log[i].s[0] );
			printf("%d ", log[i].s[1] );
			printf("%d ", log[i].s[2] );
			printf("%d ", log[i].s[3] );
			printf("%d ", log[i].s[4] );
			printf("%d ", log[i].s[5] );
			printf("%d ", log[i].lift_point );
			printf("\n");
		}
		printf("\n");
		printf("$"); //signal end of transmission
		log_sent = 1;
	#endif
	#ifndef LOGGING
		printf("$Failed to send logs, because logging was not enabled on quadcopter.\n");
	#endif
}

void panic() {
	YAW_CONTROL_LOOP = FALSE;
	FULL_CONTROL_LOOP = FALSE;
	ENABLE_INTERRUPT(INTERRUPT_GLOBAL);
	DISABLE_INTERRUPT(INTERRUPT_TIMER1);
	DISABLE_INTERRUPT(INTERRUPT_PRIMARY_RX);
	mode = PANIC_MODE_INT;
	printf("$********Going to PANIC_MODE!********\n");
	if (ae[0] > 400)
	{
		SET_ALL_ENGINE_RPM(300);
		epileptic_delay_ms(500);
		SET_ALL_ENGINE_RPM(250);
		epileptic_delay_ms(500);	
	}
	printf("$********Engines decreased!**********\n");
	print_state();
	if (ae[0] >= 250)
	{
		SET_ALL_ENGINE_RPM(200);
		epileptic_delay_ms(500);
		SET_ALL_ENGINE_RPM(150);
		epileptic_delay_ms(500);
	}
	printf("$********Engines decreased!**********\n");
	print_state();
	if (ae[0] >= 150)
	{
		SET_ALL_ENGINE_RPM(100);
		epileptic_delay_ms(500);
		SET_ALL_ENGINE_RPM(50);
		epileptic_delay_ms(500);
	}
	SET_ALL_ENGINE_RPM(0);
	printf("$********Going to SAFE MODE!*********\n");
	ENABLE_INTERRUPT(INTERRUPT_TIMER1);
	ENABLE_INTERRUPT(INTERRUPT_PRIMARY_RX);
	mode = SAFE_MODE_INT;
	// NOTE: INTERRUPT_GLOBAL is enabled now!
}

/*------------------------------------------------------------------
 * Calibrate the sensors
 *------------------------------------------------------------------
*/
void calibrate(void)
{
	int i;
	DISABLE_INTERRUPT(INTERRUPT_GLOBAL);
	sax0 = say0 = saz0 = sp0 = sq0 = sr0 = 0;
	for (i = 0; i < CALIBRATE_ITERATIONS; i++)
	{
		sax0 += sax;
		say0 += say;
		saz0 += saz;
		sp0 += sp;
		sq0 += sq;
		sr0 += sr;
		delay_ms(2);
	}

	sax0 = sax0 >> 7;
	say0 = say0 >> 7;
	saz0 = saz0 >> 7;
	sp0 = sp0 >> 7;
	sq0 = sq0 >> 7;
	sr0 = sr0 >> 7;
	ENABLE_INTERRUPT(INTERRUPT_GLOBAL);
}

/*------------------------------------------------------------------
 * Periodic execution via Timer1 interrupt
 *------------------------------------------------------------------
*/
void periodic(void) {
//	startTimestamp = X32_us_clock; //time measurements
	if ((mode == YAW_CONTROL_INT) && (YAW_CONTROL_LOOP == TRUE))
		{
			DISABLE_INTERRUPT(INTERRUPT_GLOBAL);

			// Yaw feedback control with Butterworth filter
			zr_v = convertIntToFP(zr());
			zr_filtered_old = fp_sub(fp_add(fp_mul(a0, zr_v), fp_mul(a1, zr_old)), fp_mul(b1, zr_filtered_old));
			zr_old = zr_v;
			zr_filtered = convertFPToInt(zr_filtered_old);

			// Apply corrections to current engine values
			ae[0] = within_bounds(lift_setpoint_rpm + (yaw - zr_filtered) * yaw_p,240,800);
			ae[2] = ae[0];
			ae[1] = within_bounds(lift_setpoint_rpm - (yaw - zr_filtered) * yaw_p,240,800);
			ae[3] = ae[1];

			ENABLE_INTERRUPT(INTERRUPT_GLOBAL);
		} 
	else if ((mode == FULL_CONTROL_INT) && (FULL_CONTROL_LOOP == TRUE))
		{
			DISABLE_INTERRUPT(INTERRUPT_GLOBAL);
            
            // kalman filter calculations
			p_kalman = fp_sub(sp_old, p_bias_old);
			phi_kalman = fp_add(phi_kalman_old ,fp_mul(p_kalman, p2phi));
			phi_error = fp_sub(phi_kalman, zay());
			phi_kalman = fp_sub(phi_kalman, fp_mul(phi_error, C1_inv) );
			p_bias = fp_add(p_bias, fp_mul(phi_error, fp_mul(p2phi_inv, C2_inv)));

			q_kalman = fp_sub(sq_old, q_bias_old);
			theta_kalman = fp_add(theta_kalman_old, fp_mul(q_kalman, p2phi));
			theta_error = fp_sub(theta_kalman, zax());
			theta_kalman = fp_sub(theta_kalman, fp_mul(theta_error, C1_inv) );
			q_bias = fp_add(q_bias, fp_mul(theta_error, fp_mul(p2phi_inv, C2_inv)));

			// Yaw Control Feedback Control
			zr_v = convertIntToFP(zr());
			zr_filtered_old = fp_sub(fp_add(fp_mul(a0, zr_v), fp_mul(a1, zr_old)), fp_mul(b1, zr_filtered_old));
			zr_old = zr_v;
			zr_filtered = convertFPToInt(zr_filtered_old); 

			theta_kalman = convertFPToInt(theta_kalman);
			q_kalman = convertFPToInt(q_kalman);
			phi_kalman = convertFPToInt(phi_kalman);
			p_kalman = convertFPToInt(p_kalman);
             

            //These are the correction control values for yaw,pitch and roll 
			full_yaw 	= (yaw - zr_filtered) * yaw_p;
			full_pitch 	= (full_p1 * (pitch - theta_kalman)) - (full_p2 * q_kalman);
			full_roll 	= (full_p1 * (roll - phi_kalman)) - (full_p2 * p_kalman);
             
            //Assign control values to the motors
			ae[0] = within_bounds(lift_setpoint_rpm + (full_pitch - full_yaw),240,800);
			ae[2] = within_bounds(lift_setpoint_rpm - (full_pitch - full_yaw),240,800);

			ae[1] = within_bounds(lift_setpoint_rpm - (full_roll + full_yaw),240,800);
			ae[3] = within_bounds(lift_setpoint_rpm + (full_roll + full_yaw),240,800);

			sp_old = zp();
			p_bias_old = p_bias;
			phi_kalman_old = phi_kalman;
			sq_old = zq();
			q_bias_old = q_bias;
			theta_kalman_old = theta_kalman;
			ENABLE_INTERRUPT(INTERRUPT_GLOBAL);
		} 
	//	endTimestamp = X32_us_clock - startTimestamp;
}

void initiliaze_kalman_filter()
{
	p_kalman = phi_kalman = phi_error = phi_kalman =p_bias = q_kalman = theta_kalman = theta_error = theta_kalman = q_bias = 0;
}
/*------------------------------------------------------------------
 * isr_qr_link -- QR link rx interrupt handler
 * This function is executed at 1270Hz
 * Logging can be done here as well
 *------------------------------------------------------------------
 */
void isr_qr_link(void) //1270 Hz
{
	int	ae_index;
	
	/* record time
	 */
	isr_qr_time = X32_us_clock;
    inst = X32_instruction_counter;
	
	/* get sensor and timestamp values
	 */
	sax = X32_QR_s0; 
	say = X32_QR_s1; 
	saz = X32_QR_s2; 
	sp = X32_QR_s3; 
	sq = X32_QR_s4; 
	sr = X32_QR_s5;
	timestamp = X32_QR_timestamp;

	/* monitor presence of interrupts 
	 */
	isr_qr_counter++;
	if (isr_qr_counter % 500 == 0) {
		toggle_led(2);
	}

	/* Clip engine values to be positive and 10 bits.
	 */
	for (ae_index = 0; ae_index < 4; ae_index++) 
	{
		if (ae[ae_index] < 0) 
			ae[ae_index] = 0;
		
		ae[ae_index] &= 0x3ff;
	}

	/* Send actuator values
	 * (Need to supply a continous stream, otherwise
	 * QR will go to safe mode, so just send every ms)
	 */
	X32_QR_a0 = ae[0];
	X32_QR_a1 = ae[1];
	X32_QR_a2 = ae[2];
	X32_QR_a3 = ae[3];

	/* record isr execution time (ignore overflow)
	 */
	inst = X32_instruction_counter - inst;
	isr_qr_time = X32_us_clock - isr_qr_time;

	/*
	* Logging
	*/
#ifdef LOGGING
	if ((log_counter < LOG_LENGTH) && (mode == MANUAL_MODE_INT) ) { //mode == YAW_CONTROL_INT
		log[log_counter].timestamp = X32_ms_clock;
		log[log_counter].ae[0] = (uint16_t) ae[0];
		log[log_counter].ae[1] = (uint16_t) ae[1];
		log[log_counter].ae[2] = (uint16_t) ae[2];
		log[log_counter].ae[3] = (uint16_t) ae[3];
		log[log_counter].s[0] = zax();
		log[log_counter].s[1] = zay();
		log[log_counter].s[2] = zaz();
		log[log_counter].s[3] = zp();
		log[log_counter].s[4] = zq();
		log[log_counter].s[5] = zr();
		log[log_counter].lift_point = lift_setpoint_rpm;
		log_counter++;
	}
#endif 
}

void updateControlModifiers() {
	lift_step = MAX(35+sensitivity, 35);	//35 is okay for lift
	yaw_step =  MAX(-5+sensitivity, 10);	//10 should be the minumum
	other_step = MAX(-15+sensitivity, 10);	//10 should be the minumum
}

/*------------------------------------------------------------------
 * isr_rs232_rx -- rs232 rx interrupt handler
 *------------------------------------------------------------------
 */
void isr_rs232_rx(void)
{
	int	c;

	/* signal interrupt
	 */
	toggle_led(1);

	/* may have received > 1 char before IRQ is serviced so loop
	 */
	while (X32_rs232_char) {
		fifo[iptr++] = X32_rs232_data;
#ifdef DEBUG
printf("ISR uart: iptr: %d", iptr-1);		
printf(" => %x\n", fifo[iptr-1]);
#endif
		if (iptr >= FIFOSIZE)
			iptr = 0;
	}

}


/*------------------------------------------------------------------
 * get_packet -- construct packet. return -1 on failure.
 *------------------------------------------------------------------
 */
 

void move_optr()
{	
	if (optr == FIFOSIZE-1)
		optr = 0;
	else optr++;
}

int get_packet(void)
{
	uint8_t	c;
	if (optr == iptr) //nothing to process
		return -1;

	c = (uint8_t)fifo[optr++];
	if (c == HEADER) //start of the packet
		{
			fifo[optr-1] = 0x00; //corrupt the header, otherwise we get into loops later
			if (optr==FIFOSIZE) optr = 0;

			modecommand	= (uint8_t)fifo[optr];
			move_optr();
			data1 =	(uint8_t)fifo[optr];
			move_optr();
			data2 =	(uint8_t)fifo[optr];
			move_optr();
			data3 =	(uint8_t)fifo[optr];
			move_optr();
			data4 =	(uint8_t)fifo[optr];
			move_optr();
			checksum = (uint8_t)fifo[optr];
			move_optr();
			checker = VERIFY_CHECKSUM(modecommand, data1, data2, data3, data4, checksum);

			//hack, because we shouldn't be getting this error. it somehow gets out of sync
			if (iptr != optr) return -1;

			#ifdef DEBUG
			printf("\niptr is: %d,  optr id: %d \n", iptr, optr);
			printf("mode is: %x \n", modecommand);
			printf("LIFT is: %x \n", data1);
			printf("YAW is: %x \n", data2);
			printf("PITCH is: %x \n", data3);
			printf("ROLL is: %x \n", data4);
			printf("Checksum is: %x \n", checksum);
			printf("%s\n", checker==0 ? "PASS" : "FAIL");
			#endif
			
			//check checksum
			if ( (int)checker != 0)
			{
				#ifdef DEBUG
				printf("Invalid packet recieved! Discarding!\n");
				#endif
			 return -1; //ERROR, invalid packet
			}						//such that we do this check only once
		}
	else
	{
		//reset buffer pointers if we get out of sync
		optr = iptr = 0;
		return -1;
	}
return 0;
}


/*------------------------------------------------------------------
 * delay_ms -- busy-wait for ms milliseconds
 *------------------------------------------------------------------
 */
void delay_ms(int ms) 
{
	int time = X32_ms_clock;
	while(X32_ms_clock - time < ms);
}


void epileptic_delay_ms(int ms) 
{
	int time = X32_ms_clock;
	while(X32_ms_clock - time < ms) {
		if(X32_ms_clock % 10 == 0)
		toggle_led(0);
	}
}

/*------------------------------------------------------------------
 * delay_us -- busy-wait for us microseconds
 *------------------------------------------------------------------
 */
void delay_us(int us) 
{
	int time = X32_us_clock;
	while(X32_us_clock - time < us);
}

/*------------------------------------------------------------------
 * toggle_led -- toggle led # i
 *------------------------------------------------------------------
 */
void toggle_led(int i) 
{
	X32_leds = (X32_leds ^ (1 << i));
}


/*------------------------------------------------------------------
 * print_state -- print all sensors and actuators
 *------------------------------------------------------------------
 */
void print_state(void) 
{
	int i;
	printf("%3d %3d %3d %3d | ",ae[0],ae[1],ae[2],ae[3]);
	printf("%3d %3d %3d %3d %3d %3d | %d %d %d %d \n",
		zax(),zay(),zaz(),zp(),zq(),zr(), yaw_p, full_p1, full_p2, sensitivity);
}

int within_bounds(int x, int lower_limit, int upper_limit) {
	if(x > upper_limit) {
		return upper_limit;
	}
	if(x < lower_limit) {
		return lower_limit;
	}
	return x;
}

int process_data_field (uint8_t* data, uint8_t* data_old, int* knob)
{
	if (*data != *data_old) //save time if no changes in pitch input
	{
		*data_old = *data;
		if ( (*data&0x10) == 0x00) 
			{
				*knob  = ((int)*data&0x0F)*2; //the coeff
			}
		else
			{
				*knob = ((int)*data&0x0F);
				*knob = (*knob) *(-2);
			}
		return 1;
	}
	else 
		return 0; //no change in data field
}

void reset_leds() {
	X32_leds = 0;
}
