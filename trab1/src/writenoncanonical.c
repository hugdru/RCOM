/*Non-Canonical Input Processing*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>

#define BAUDRATE B38400
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

// Trama
#define F 0x7e
#define A 0x03
#define C 0x03
#define B A^C

#define TIMEOUT 3

typedef enum { false, true } bool;
volatile int STOP = FALSE;
static bool alarmed = false;
static uint8_t SET[5];
static char buf[255];

int controlledWrite( int fd, uint8_t *packetBundle, size_t nPackets);
static void alarm_handler(int signo);

int main(int argc, char** argv)
{
    int fd;
    struct termios oldtio,newtio;
    // Trama
    SET[0] = F;
    SET[1] = A;
    SET[2] = C;
    SET[3] = B;
    SET[4] = F;

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

    newtio.c_cc[VTIME]    = 1;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 0;   /* blocking read until 5 chars received */

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

    controlledWrite(fd,SET,sizeof(SET));

    sleep(3);

    if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    close(fd);
    return 0;
}

int controlledWrite(int fd, uint8_t *packetBundle, size_t nPackets) {

    ssize_t res;
    size_t numberOfTries = 0, readPackets = 0, i = 0;
    char Estado = 0, packetPartOfUA;
    bool jump = false;

    if ( (packetBundle == NULL) || ( nPackets == 0) ) {
        errno = EINVAL;
        return -1;
    }

    if (signal(SIGALRM, alarm_handler) == SIG_ERR)
        return -1;

    while(1) {
        if ( Estado == 0 ) {
            write(fd,SET,nPackets);
            alarm(TIMEOUT);
            Estado = 1;
        } else {
            while(!alarmed) {
                    res = read(fd,&packetPartOfUA,1);
                    if ( readPackets == nPackets ) alarmed = true;
                    else if ( res == 1 ) {
                        buf[readPackets] = packetPartOfUA;
                        ++readPackets;
                    }
            }
            if ( readPackets == nPackets ) {
                for ( i = 0; i < nPackets; ++i ) {
                    if ( buf[i] != SET[i] ) {
                        jump = true;
                        break;
                    }
                }
                if (!jump) return 0;
            }

            if ( numberOfTries >= 3 ) {
                errno = ECONNABORTED;
                return -1;
            } else {
                Estado = 0;
                alarmed = false;
                jump = false;
                readPackets = 0;
            }
            ++numberOfTries;
        }
    }
    return -1;
}

static void alarm_handler(int signo) {
    alarmed = true;
}
