#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define BAUDRATE B38400
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

// SET/UA bytes {F, A, C, B, F}
#define F 0x7E
#define A 0x03
#define C 0x03
#define B A^C

unsigned int state; // State in UA state machine
int fd; // Pipe
int flag;
struct termios oldtio, newtio; // Old and new Port settings

/**
 * Function called when alarm signal is received
 */
void alarm_handler()
{
	printf("alarme\n");
	flag = 0;
}


int llopen() {
	unsigned char ch, UA[5];
	int tries = 0;
	state = 0;

	/*
	 Open serial port device for reading and writing and not as controlling tty
	 because we don't want to get killed if linenoise sends CTRL-C.
	 */
	fd = open(MODEMDEVICE, O_RDWR | O_NOCTTY);

	if (fd < 0) {
		printf("Error initializing pipe fd\n");
		exit(-1);
	}

	if (tcgetattr(fd, &oldtio) == -1) { /* save current port settings */
		perror("tcgetattr");
		exit(-1);
	}

	bzero(&newtio, sizeof(newtio));
	newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;

	newtio.c_lflag = 0;	/* set input mode (non-canonical, no echo,...) */
	newtio.c_cc[VTIME] = 1; /* inter-character timer unused */
	newtio.c_cc[VMIN] = 0; /* blocking read until 5 chars received */

	/*
	 VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
	 leitura do(s) próximo(s) caracter(es)
	 */
	tcflush(fd, TCIOFLUSH);

	if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
		perror("tcsetattr");
		exit(-1);
	}
	printf("New termios structure set\n");

	UA[0] = F;
	UA[1] = A;
	UA[2] = C;
	UA[3] = A^C;
	UA[4] = F;

	signal(SIGALRM, alarm_handler);  // Sets function alarm_handler as the handler of alarm signals

	while(tries < 3) {

		flag = 1;
		alarm(3);

		while(flag) {
			read(fd, &ch, 1);

			switch (state) {
				case 0:
					if (ch == F)
						state = 1;
					break;
				case 1:
					if (ch == A)
						state = 2;
					else if (ch != F)
						state = 0;
					break;
				case 2:
					if (ch == C)
						state = 3;
					else if (ch == F)
						state = 1;
					else
						state = 0;
					break;
				case 3:
					if (ch == B)
						state = 4;
					else if (ch == F)
						state = 1;
					else
						state = 0;
					break;
				case 4:
					if (ch == F)
						state = 5;
					else
						state = 0;
					break;
				case 5:
					write(fd, UA, 5); // Send UA
					return 0; //Succeded
					break;
			}
		}
		tries++;
	}
	return 1; //Failed
}

void llread() {

}

void llclose() {
	if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) { /* Restores old port settings */
		perror("tcsetattr");
		exit(-1);
	}

	close(fd);
}

int main(int argc, char** argv) {
	if(llopen() == 1) {
		printf("Error: llopen() failed\n");
	}
	else
		printf("llopen() was successful\n");

	llclose();
	return 0;
}

