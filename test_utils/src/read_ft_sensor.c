
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define DATA_LENGTH 8
#define ARDUINO_CAN_ID 0x64
#define FTSENSOR_1_CAN_ID 0xF8
#define FTSENSOR_1_CAN_DATA_ID_1 0xF9  // CAN ID for the 1st response package 
#define FTSENSOR_1_CAN_DATA_ID_2 0xFA  // CAN ID for the 2nd response package 
#define FT_START_DATA_OUTPUT 0x0B  // refer to Robotous datasheet

#define FT_CORRECTION_DF 50.0    // refer to Robotous datasheet
#define FT_CORRECTION_DT 1000.0  // refer to Robotous datasheet

volatile sig_atomic_t flag = 0;
void my_function(int sig){ // can be called asynchronously
  flag = 1; // set flag
}

// for save csv
// https://stackoverflow.com/questions/4955360/csv-file-generator-in-c

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
    // printf("milliseconds: %lld\n", milliseconds);
    return milliseconds;
}

int main(int argc, char **argv)
{
	// CAN related variables
	int s, i;
	int nbytes;
	struct sockaddr_can addr;
	struct ifreq ifr;
	struct can_frame frame;

	// Data variables
	float forceX = 0.0;
	float forceY = 0.0;
	float forceZ = 0.0;
	float momntX = 0.0;
	float momntY = 0.0;
	float momntZ = 0.0;

	// Tempory CAN frame data field
	char ftDataPack1_temp[8] = {0};  // save data from pack 1 here temporarily
	char ftDataPack1[8] = {0};
	char ftDataPack2[8] = {0};

	// Data writing operation
	char writeFlag = 0;  // if it is 1, writing csv is permitted
	char ftFinished = 0; // set to 1 when 2 response package received consecutively

	// Time
	long long time_ms = 0;

	// File writing
	char *filename = argv[1];
	char *isRecord = argv[2];

	signal(SIGINT, my_function); 

	printf("CAN Sockets Receive Demo\r\n");

	// Create a socket
	if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("Socket");
		return 1;
	}

	// Retrieve the interface index for the interface name (can0, can1, vcan0 etc) we wish to use.
	strcpy(ifr.ifr_name, "can0" );

	// Send an I/O control call and pass an ifreq structure (ifr) containing the interface name:
	ioctl(s, SIOCGIFINDEX, &ifr);

	// Bind the socket to the CAN Interface:
	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("Bind");
		return 1;
	}

	// Command FT Senosr to start transmiting
	frame.can_id = FTSENSOR_1_CAN_ID;
	frame.can_dlc = 8;  // data field length
	frame.data[0] = FT_START_DATA_OUTPUT;  // start FT data ouput

	// Send it using the write() system call
	if (write(s, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
		perror("Write");
		return 1;
	}

	// Open file
	// "20230209_test_T07.csv"
	FILE * fp;
	fp = fopen (filename, "w+");

	while(1) {

		// Call the read() system call. This will block until a frame is available
		nbytes = read(s, &frame, sizeof(struct can_frame));
	 	if (nbytes < 0) {
			perror("Read");
			return 1;
		}
		//if (frame.can_dlc != DATA_LENGTH) {
		//	continue;
		//}
		if (frame.can_id == FTSENSOR_1_CAN_DATA_ID_1) {
			writeFlag = 0;
			// only save the data temporarily, before pack 2 is received.
			// to prevent Arduino data received between pack 1 and 2, and written to csv.
			for (int i = 0; i <= 7; i++) {
				ftDataPack1_temp[i] = frame.data[i];
				ftFinished = 0;
			}
		}
		else if (frame.can_id == FTSENSOR_1_CAN_DATA_ID_2) {
			// only update ft data, simultanously, after pack 2 is received
			for (int i = 0; i <= 7; i++) {
				ftDataPack1[i] = ftDataPack1_temp[i]; // put temp data to usable data
				ftDataPack2[i] = frame.data[i];       // put temp data to usable data
				ftFinished = 1;
                writeFlag = 1;
                // Parse FT sensor CAN data
                forceX = (short)((ftDataPack1[1] << 8) | ftDataPack1[2]) / FT_CORRECTION_DF;
                forceY = (short)((ftDataPack1[3] << 8) | ftDataPack1[4]) / FT_CORRECTION_DF;
                forceZ = (short)((ftDataPack1[5] << 8) | ftDataPack1[6]) / FT_CORRECTION_DF;
                momntX = (short)((ftDataPack1[7] << 8) | ftDataPack2[0]) / FT_CORRECTION_DT;
                momntY = (short)((ftDataPack2[1] << 8) | ftDataPack2[2]) / FT_CORRECTION_DT;
                momntZ = (short)((ftDataPack2[3] << 8) | ftDataPack2[4]) / FT_CORRECTION_DT;

                // Get time
                time_ms = current_timestamp();
			}
		}

		if (writeFlag == 1) {
			// after the 2nd frame is received, save the data for all sensors
			// including previously saved FT sensor data
			// print data from both Arduino and FT sensor
			// fsr, accX, accY, accZ, forceX, forceY, forceZ, momntX, momntY, momntZ
			printf("%lld, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f", time_ms, forceX, forceY, forceZ, momntX, momntY, momntZ);
			printf("\r\n");
		}

		if(flag){ // my action when signal set it 1
			printf("\n Exit!\n");
			flag = 0;

			fclose(fp);
			printf("\n File closed \n ");

			if (close(s) < 0) {
				perror("Close");
				return 1;
			}

			printf("\n CAN socket closed \n ");
			
			return 0;
	    }

	}

	// Close the socket

	if (close(s) < 0) {
		perror("Close");
		return 1;
	}
	fclose(fp);
	printf("file closed");

	return 0;
}