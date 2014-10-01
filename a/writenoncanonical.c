/*Non-Canonical Input Processing*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <strings.h>

#define BAUDRATE B38400
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

#define F 0x7E
#define A 0x03
#define C 0x03

volatile int STOP=FALSE;
int flag=1, tries=0, received=0;

void alarm_handler()                   // atende alarme
{
	printf("alarme # %d\n", tries);
	flag=1;
	tries++;
}

int main(int argc, char** argv)
{
    int fd,c, res;
    struct termios oldtio,newtio;
    char buf[255];
    int i, sum = 0, speed = 0;

    if ( (argc < 2) ||
         ((strcmp("/dev/ttyS0", argv[1])!=0) &&
          (strcmp("/dev/ttyS1", argv[1])!=0) )) {
      printf("Usage:\tnserial SerialPort\n\tex: nserial /dev/ttyS1\n");
      exit(1);
    }


  /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
  */


    fd = open(argv[1], O_RDWR | O_NOCTTY );
    if (fd <0) {perror(argv[1]); exit(-1); }

    if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 5;   /* blocking read until 5 chars received */

  /*
    VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
    leitura do(s) próximo(s) caracter(es)
  */

    tcflush(fd, TCIOFLUSH);

    if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    printf("New termios structure set\n");

    printf("Write the information to send\n");
    fgets(buf, sizeof(buf), stdin);
    buf[sizeof(buf) - 1] = 0;
    printf("Size of buffer %d\n", sizeof(buf));
    printf("Length of string: %d\n", strlen(buf));
    printf("The message is: %s", buf);

	unsigned char SET[5];

	SET[0] = F;
	SET[1] = A;
	SET[2] = C;
	SET[3] = SET[1]^SET[2];
	SET[4] = F;

	(void) signal(SIGALRM, alarm_handler);  // instala  rotina que atende interrupcao

	while(tries < 3){
   		if(!received){
			write(fd, SET, 5);
      			alarm(3);                 // activa alarme de 3s
			//res = read(fd,SET,5);
      			flag=0;
   		}
		else {
			printf("Received UA\n");
			break;
		}
	}
	if(!received) {
		printf("Failed to syncronize\n");
		exit(1);
	 }
	
	

    res = write(fd,buf,strlen(buf)+1);
    printf("%d bytes written\n", res);

    sleep(3);

    if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    close(fd);
    return 0;
}
