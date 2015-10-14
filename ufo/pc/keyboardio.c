#include "keyboardio.h"
#include "consoleio.h"
#include "input.h"
#include "tui.h"

//TUI related
extern int msg_cursor;

void processKeyboardEvent(char c, struct INPUT* keyboardInput, struct SPECIAL_INPUT* p_input) {
	bool updated;
	if ((c = term_getchar_nb()) != -1) {
		updated = true;
		bool unknownKey = true;
		if(keyboardInput->mode != SAFE_MODE_INT && keyboardInput->mode != PANIC_MODE_INT) {
			unknownKey = false;
			switch(c) {
				// Controls
				case 'a':
					if (keyboardInput->lift < MAX_LEVEL) {	
						keyboardInput->lift++;
					}
					break;
				case 'z':
					if (keyboardInput->lift > -MAX_LEVEL) {
						keyboardInput->lift--;
					}
					break;

				case 'q': // yaw left
					if (keyboardInput->yaw > -MAX_LEVEL) {
						keyboardInput->yaw--;
					}
					break;
				case 'w': // yaw right
					if (keyboardInput->yaw < MAX_LEVEL) {
						keyboardInput->yaw++;
					}
					break;
				case 65: // pitch up
					if (keyboardInput->pitch < MAX_LEVEL) {
						keyboardInput->pitch++;
					}
					break;
				case 66: // pitch down
					if (keyboardInput->pitch > -MAX_LEVEL) {
						keyboardInput->pitch--;
					}
					break;
				case 68: // roll right
					if (keyboardInput->roll > -MAX_LEVEL) {
						keyboardInput->roll--;
					}
					break;
				case 67: // roll right
					if (keyboardInput->roll < MAX_LEVEL) {
						keyboardInput->roll++;
					}
					break;
				default:
					unknownKey = true;
					break;
			}
		}	

		switch(c) {

			// Modes 
			case '0': // Safe mode
				//printf("Set to safe mode (%x)\n", SAFE_MODE_INT);
				TUI_PRINT_MESSAGE(Set to safe mode);
				keyboardInput->mode = SAFE_MODE_INT;
				break;
			case '1': // Panic mode
				//printf("Set to panic mode (%x)\n", PANIC_MODE_INT);
				TUI_PRINT_MESSAGE(Set to panic mode);
				keyboardInput->mode = PANIC_MODE_INT;
				break;
			case '2': // Manual mode
				//printf("Set to manual mode (%x)\n", MANUAL_MODE_INT);
				TUI_PRINT_MESSAGE(Set to manual mode);
				keyboardInput->mode = MANUAL_MODE_INT;
				break;
			case '3': // Calibrate mode
				//printf("Set to calibrate mode (%x)\n", CALIBRATE_MODE_INT);
				TUI_PRINT_MESSAGE(Set to calibrate mode);
				keyboardInput->mode = CALIBRATE_MODE_INT;
				break;
			case '4': // Yaw control mode
				//printf("Set to 'Yaw control' mode (%x)\n", YAW_CONTROL_INT);
				TUI_PRINT_MESSAGE(Set to 'Yaw control' mode);
				keyboardInput->mode = YAW_CONTROL_INT;
				break;
			case '5': // Manual mode
				//printf("Set to 'Full Control' mode (%x)\n", FULL_CONTROL_INT);
				TUI_PRINT_MESSAGE(Set to 'Full Control' mode);
				keyboardInput->mode = FULL_CONTROL_INT;
				break;
			case 27: // Escape
				//printf("Not implemented yet!\n");
				TUI_PRINT_MESSAGE(Not implemented yet!);
				updated = false;
				break;

			// Controls for p values
			case 'u': // p control yaw
				//printf("Incremented P value for YAW!\n");
				TUI_PRINT_MESSAGE(Incremented P value for YAW!);
				p_input->yaw_p = 1;
				p_input->updated = true;
				break;
			case 'j': // p control yaw
				//printf("Decremented P value for YAW!\n");
				TUI_PRINT_MESSAGE(Decremented P value for YAW!);
				p_input->yaw_p = -1;
				p_input->updated = true;
				break;
			case 'i': // p1 control pitch/roll
				//printf("Not implemented yet!\n");
				TUI_PRINT_MESSAGE(Not implemented yet!);
				updated = false;
				break;
			case 'k': // p1 control pitch/roll
				//printf("Not implemented yet!\n");
				TUI_PRINT_MESSAGE(Not implemented yet!);
				updated = false;
				break;
			case 'o': // p2 control pitch/roll
				//printf("Not implemented yet!\n");
				TUI_PRINT_MESSAGE(Not implemented yet!);
				updated = false;
				break;
			case 'l': // p2 control pitch/roll
				//printf("Not implemented yet!\n");
				TUI_PRINT_MESSAGE(Not implemented yet!);
				updated = false;
				break;

			// Additional controls
			case '+': // Upload metrics
				//printf("Set to 'LOGGING' mode (%x)\n", SEND_TELEMETRY_INT);
				TUI_PRINT_MESSAGE(Set to 'LOGGING' mode);
				keyboardInput->mode = SEND_TELEMETRY_INT;
				break;

			default:
				if(unknownKey) {
					//printf("Unknown key: %i\n", c);
					mvprintw(MESSAGE_FIELD_START + msg_cursor,0,"Unknown key: %i\n", c);
					TUI_MOVE_CURSOR;
					updated = false;
				}
				// TODO: add other keys for P
		}
		keyboardInput->updated = updated;
	}
}