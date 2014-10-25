#include "linklayer.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <strings.h>
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>

#define F 0x7e
#define A_CSENDER_RRECEIVER 0x03
#define A_CRECEIVER_RSENDER 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x11
#define C_RR_RAW 0x05
#define C_REJ_RAW 0x01

#define ESC 0x7d
#define STUFFING_XOR_BYTE 0x20

#define SIZE_OF_FRAMESU 5
#define CRC_8 0x9b

typedef struct LinkLayer {
    bool is_receiver;
    unsigned int sequenceNumber;
    int serialFileDescriptor;
    struct termios oldtio;
    LinkLayerSettings *settings;
} LinkLayer;

void alarm_handler(int signo);
uint8_t* buildFrameHeader(uint8_t A, uint8_t C, uint16_t *frameSize);
int buildRawFramesBodies(uint8_t *packet, size_t packetSize, uint8_t **payloadsAndFooter, size_t *nPayloadsAndFootersToProcess); // Build the part of the data and trailer without the stuffing
uint8_t generateBcc(const uint8_t *data, uint16_t size);

LinkLayer LLayer;

// Tem de ser global por causa do llclose()
size_t leftOversSize = 0;
uint8_t *payloadsAndFooterLeftOver = NULL;

bool blocked = false;
bool alarmed = false;

int llinitialize(LinkLayerSettings *ptr, bool is_receiver) {

    if ( blocked ) {
        fprintf(stderr, "You can only initialize once\n");
        return -1;
    }

    payloadsAndFooterLeftOver = (uint8_t *) malloc( sizeof(uint8_t) * LLayer.settings->IframeSize );

    if ( ptr == NULL ) {
        errno = EINVAL;
        return -1;
    }

    LLayer.settings = ptr;
    LLayer.is_receiver = is_receiver;
    payloadsAndFooterLeftOver = NULL;

    blocked = true;
    return 0;
}

int llopen(void) {

    bool first;
    unsigned int tries = 0;
    struct termios newtio;
    uint8_t partOfFrame, state;
    uint8_t *frameToSend;
    uint16_t frameSize;

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
        close(LLayer.serialFileDescriptor);
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
        close(LLayer.serialFileDescriptor);
        return -1;
    }
    // New termios Structure set
    signal(SIGALRM, alarm_handler);  // Sets function alarm_handler as the handler of alarm signals

    alarmed = false;
    while(tries < LLayer.settings->numAttempts) {

        if ( !LLayer.is_receiver ) {
            frameToSend = buildFrameHeader(A_CSENDER_RRECEIVER, C_SET, &frameSize);
            frameToSend = (uint8_t *) realloc(frameToSend, ++frameSize);
            if ( frameToSend == NULL ) goto cleanUp;
            frameToSend[frameSize-1] = F;
            write(LLayer.serialFileDescriptor, frameToSend, 5);
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
                    if (partOfFrame == A_CSENDER_RRECEIVER)
                        state = 2;
                    else if (partOfFrame != F)
                        state = 0;
                    break;
                case 2:
                    if (partOfFrame == C_SET)
                        state = 3;
                    else if (partOfFrame == F)
                        state = 1;
                    else
                        state = 0;
                    break;
                case 3:
                    if (partOfFrame == (A_CSENDER_RRECEIVER ^ C_SET))
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
                        frameToSend = buildFrameHeader(A_CSENDER_RRECEIVER, C_UA, &frameSize);
                        frameToSend = (uint8_t *) realloc(frameToSend, ++frameSize);
                        if ( frameToSend == NULL ) goto cleanUp;
                        frameToSend[frameSize-1] = F;
                        write(LLayer.serialFileDescriptor,frameToSend,SIZE_OF_FRAMESU);
                    }
                    free(frameToSend);
                    return 0;
                default:
                    return -1;
            }
        }
        alarmed = false;
        tries++;
    }
    errno = ECONNABORTED;
cleanUp:
    free(frameToSend);
    tcsetattr(LLayer.serialFileDescriptor,TCSANOW,&(LLayer.oldtio));
    close(LLayer.serialFileDescriptor);
    return -1;
}

int llwrite(uint8_t *packet, size_t packetSize) {

    size_t nPayloadsAndFootersToProcess = 0;
    uint8_t **payloadsAndFooter = NULL;

    buildRawFramesBodies(packet,packetSize,payloadsAndFooter,&nPayloadsAndFootersToProcess);
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

    if ( !blocked ) {
        fprintf(stderr, "You have to llinitialize and llopen first\n");
        return -1;
    }

    // SEND LeftOverFrame if there is one



    // END Connection in a proper way



    sleep(3);

    free(payloadsAndFooterLeftOver);
    leftOversSize = 0;

    if ( tcsetattr(LLayer.serialFileDescriptor,TCSANOW,&(LLayer.oldtio)) == -1 ) {
      perror("tcsetattr");
      return -1;
    }

    close(LLayer.serialFileDescriptor);
    blocked = false;
    return 0;
}

void alarm_handler(int signo) {
    alarmed = true;
}

uint8_t* buildFrameHeader(uint8_t A, uint8_t C, uint16_t *frameSize) {
    uint8_t *tempHeader;
    uint8_t temp;
    uint8_t size = 4;

    if ( frameSize == NULL ) {
        errno = EINVAL;
        return NULL;
    }

    tempHeader = (uint8_t *) malloc( sizeof(uint8_t) * size);
    tempHeader[0] = F;
    tempHeader[1] = A;
    tempHeader[2] = C;
    tempHeader[3] = generateBcc(tempHeader+1, 2);

    if ( tempHeader[3] == F  || tempHeader[3] == ESC ) {
        size++;
        tempHeader = (uint8_t *) realloc(tempHeader, size);
        temp = tempHeader[3];
        tempHeader[3] = ESC;
        tempHeader[4] = ESC ^ STUFFING_XOR_BYTE;
    }

    *frameSize = size;
    return tempHeader;
}

int buildRawFramesBodies(uint8_t *packet, size_t packetSize, uint8_t **payloadsAndFooter, size_t *nPayloadsAndFootersToProcess) {

    uint8_t xorMe;
    size_t i, t;
    size_t temp;
    size_t nCompletePayloadsAndFooters;
    unsigned int fillUntil;

    if ( packet == NULL || nPayloadsAndFootersToProcess == NULL ) return -1;

    if ( leftOversSize != 0 ) {
        if ( packetSize >= (LLayer.settings->IframeSize - leftOversSize) ) {
            payloadsAndFooter = (uint8_t **) realloc(payloadsAndFooter, *nPayloadsAndFootersToProcess);
            payloadsAndFooter[*nPayloadsAndFootersToProcess] = (uint8_t *) malloc( sizeof(uint8_t) * LLayer.settings->IframeSize + 2 );
            fillUntil = LLayer.settings->IframeSize;
            for ( i = 0; i < leftOversSize; ++i) {
                payloadsAndFooter[*nPayloadsAndFootersToProcess][i] = payloadsAndFooterLeftOver[i];
            }
        } else fillUntil = packetSize;

        xorMe = 0x00;
        for ( i = leftOversSize; i < fillUntil; ++i) {
            if ( fillUntil < LLayer.settings->IframeSize ) {
                payloadsAndFooterLeftOver[i] = *packet;
            } else {
                payloadsAndFooter[*nPayloadsAndFootersToProcess][i] = *packet;
                xorMe ^= *packet;
            }
            ++packet;
            --packetSize;
        }
        if ( i == LLayer.settings->IframeSize ) {
            payloadsAndFooter[*nPayloadsAndFootersToProcess][i++] = xorMe;
            payloadsAndFooter[*nPayloadsAndFootersToProcess][i] = F;
            *nPayloadsAndFootersToProcess = *nPayloadsAndFootersToProcess + 1;
            leftOversSize = 0;
        } else {
            leftOversSize = i;
            return 0;
        }
    }

    nCompletePayloadsAndFooters = packetSize / LLayer.settings->IframeSize;
    temp = *nPayloadsAndFootersToProcess + nCompletePayloadsAndFooters;
    payloadsAndFooter = (uint8_t **) realloc(payloadsAndFooter, temp);
    for( i = *nPayloadsAndFootersToProcess; i < temp; ++i) {
        payloadsAndFooter[i] = (uint8_t *) malloc( sizeof(uint8_t) * LLayer.settings->IframeSize + 2 );
        xorMe = 0x00;
        for ( t = 0; t < LLayer.settings->IframeSize; ++t) {
            payloadsAndFooter[i][t] = *packet;
            xorMe ^= *packet;
            --packetSize;
            ++packet;
        }
        payloadsAndFooter[i][t++] = xorMe;
        payloadsAndFooter[i][t] = F;
    }
    *nPayloadsAndFootersToProcess = i;

    while(packetSize--) {
        *payloadsAndFooterLeftOver = *packet;
        ++packet;
        ++leftOversSize;
    }

    return 0;
}

uint8_t generateBcc(const uint8_t *data, uint16_t size) {

    size_t i;
    uint8_t bcc = 0x00;

    if ( data == NULL ) {
        errno = EINVAL;
        return -1;
    }

    for( i = 0; i < size; ++i) {
        bcc ^= data[i];
    }

    return bcc;
}

uint8_t generate_crc8(const uint8_t *data, uint16_t size) {

    uint8_t R = 0;
    uint8_t bitsRead = 0;

    if ( data == NULL || size == 0 ) {
        errno = EINVAL;
        return 0;
    }

    // Calculate the remainder
    while( size > 0 ) {
        R <<= 1;
        R |= (*data >> bitsRead) & 0x1;
        bitsRead++;
        if ( bitsRead > 7 ) {
            bitsRead = 0;
            size--;
            data++;
        }
        if ( R & 0x80 ) R ^= CRC_8;
    }

    size_t i;
    for(i = 0; i < 8; ++i) {
        R <<=1;
        if( R & 0x80 ) R ^= CRC_8;
    }

    return R;
}

