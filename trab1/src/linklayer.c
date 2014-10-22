#include "linklayer.h"
#include "useful.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <strings.h>
#include <stdio.h>

// Useful defines for SET & UA
#define F 0x7e
#define A 0x03
#define C 0x03
#define B ((A)^(C))

#define CONTAINER_SIZE 256
#define SIZE_OF_FRAMESU 5

#define SET_OFFSET 0
#define UA_OFFSET 0
#define RR_OFFSET 1
#define DISC_OFFSET 2

bool alarmed = false;
void alarm_handler(int signo);

uint8_t framesSU[][SIZE_OF_FRAMESU] = {{F,A,C,A^C,F}, {0}, {0}};

LinkLayer LLayer;

static bool blocked = false;

int llinitialize(parsedLinkLayerSettings *ptr, bool is_receiver) {

    if ( blocked ) {
        fprintf(stderr, "You can only initialize once\n");
        return -1;
    }

    if ( ptr == NULL ) {
        errno = EINVAL;
        return -1;
    }

    LLayer.settings = ptr;
    LLayer.is_receiver = is_receiver;

    blocked = true;
    return 0;
}

int llopen(void) {

    bool first;
    unsigned int tries = 0;
    struct termios newtio;
    uint8_t partOfFrame, state;

    if ( !blocked ) {
        fprintf(stderr, "You have to llinitialize first\n");
        return -1;
    }

  /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
  */

    LLayer.serialFileDescriptor = open(LLayer.settings->port, O_RDWR | O_NOCTTY);
    if ( LLayer.serialFileDescriptor == -1 ) {
        perror(LLayer.settings->port);
        return -1;
    }

    if ( tcgetattr(LLayer.serialFileDescriptor,&(LLayer.oldtio)) == -1 ) { /* save current port settings */
        perror("tcgetattr");
        return -1;
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = LLayer.settings->baudRate | CS8 | CLOCAL | CREAD;
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

    tcflush(LLayer.serialFileDescriptor, TCIOFLUSH);

    if ( tcsetattr(LLayer.serialFileDescriptor,TCSANOW,&newtio) == -1) {
      perror("Failed to set new settings for port, tcsetattr");
      return -1;
    }
    // New termios Structure set
    signal(SIGALRM, alarm_handler);  // Sets function alarm_handler as the handler of alarm signals

    alarmed = false;
    while(tries < LLayer.settings->numAttempts) {

        if ( !LLayer.is_receiver ) {
            write(LLayer.serialFileDescriptor, framesSU[SET_OFFSET], 5);
            alarm(3);
        } else first = true;

        while(!alarmed) {
            read(LLayer.serialFileDescriptor, &partOfFrame, 1);

            switch (state) {
                case 0:
                    if (partOfFrame == F)
                        if ( LLayer.is_receiver && first ) {
                            alarm(3);
                            first = false;
                        }
                        state = 1;
                    break;
                case 1:
                    if (partOfFrame == A)
                        state = 2;
                    else if (partOfFrame != F)
                        state = 0;
                    break;
                case 2:
                    if (partOfFrame == C)
                        state = 3;
                    else if (partOfFrame == F)
                        state = 1;
                    else
                        state = 0;
                    break;
                case 3:
                    if (partOfFrame == B)
                        state = 4;
                    else if (partOfFrame == F)
                        state = 1;
                    else
                        state = 0;
                    break;
                case 4:
                    if (partOfFrame == F)
                        state = 5;
                    else
                        state = 0;
                    break;
                case 5:
                    if ( LLayer.is_receiver ) {
                        write(LLayer.serialFileDescriptor,framesSU[UA_OFFSET],SIZE_OF_FRAMESU);
                    }
                    return 0; //Succeeded
                default:
                    return -1;
            }
        }
        alarmed = false;
        tries++;
    }
    errno = ECONNABORTED;
    return -1;
}

int lwrite(uint8_t *packet, size_t packetSize) {

    return 0;
}

/*int llwrite(Tunnel *ptr) {*/

    /*ssize_t res;*/
    /*size_t numberOfTries = 0, readPackets = 0, i = 0;*/
    /*uint8_t Estado = 0, partOfReply;*/
    /*bool jump = false;*/

    /*if ( (buffer == NULL) || ( length == 0) ) {*/
        /*errno = EINVAL;*/
        /*return -1;*/
    /*}*/

    /*if (signal(SIGALRM, alarm_handler) == SIG_ERR) return -1;*/

    /*while(1) {*/
        /*if ( Estado == 0 ) {*/
            /*write(ptr->ALayer.fileDescriptor,buffer,length);*/
            /*alarm(ptr->LLayer.timeout);*/
            /*Estado = 1;*/
        /*} else {*/
            /*while(!alarmed) {*/
                    /*if ( readPackets == CONTAINER_SIZE ) {*/
                        /*errno = EBADMSG;*/
                        /*return -1;*/
                    /*} else if ( readPackets == SIZE_OF_REPLY ) { // Penso que as replies têm todas o mesmo tamanho*/
                        /*alarmed = true;*/
                        /*alarm(0); // Caso todos os pacotes já tenham chegado desativamos o alarm*/
                    /*}*/
                    /*res = read(ptr->ALayer.fileDescriptor,&partOfReply,1);*/
                    /*if ( res == 1 ) {*/
                        /*byteContainer[readPackets] = partOfReply;*/
                        /*++readPackets;*/
                    /*}*/
            /*}*/
            /*// Podemos ter recebido o sinal sem termos recebido os pacotes todos*/
            /*// Dependendo do que queremos mandar temos de saber verificar as replies do receptor! São diferentes para diferentes situações*/
            /*// Tem de ser adicionado alguma coisa às structs para saber que reply tenho de testar*/
            /*[>if ( readPackets == CONTAINER_SIZE ) {<]*/
                /*[>for ( i = 0; i < length; ++i ) {<]*/
                    /*[>if ( buffer[i] != byteContainer[i] ) {<]*/
                        /*[>jump = true;<]*/
                        /*[>break;<]*/
                    /*[>}<]*/
                /*[>}<]*/
                /*[>if (!jump) return 0;<]*/
            /*[>}<]*/

            /*if ( numberOfTries >= ptr->LLayer.numAttempts ) {*/
                /*errno = ECONNABORTED;*/
                /*return -1;*/
            /*} else {*/
                /*Estado = 0;*/
                /*alarmed = false;*/
                /*jump = false;*/
                /*readPackets = 0;*/
            /*}*/
            /*++numberOfTries;*/
        /*}*/
    /*}*/
    /*return -1;*/
/*}*/

int llread(uint8_t *packet) {
    return 0;
}

int llclose(void) {

    sleep(3);

    if ( tcsetattr(LLayer.serialFileDescriptor,TCSANOW,&(LLayer.oldtio)) == -1 ) {
      perror("tcsetattr");
      return -1;
    }

    close(LLayer.serialFileDescriptor);
    return 0;
}

void alarm_handler(int signo) {
    alarmed = true;
}

