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

#define CRC_8 0x9b

#define IS_IFRAMEHEAD true
#define IS_FRAMESUHEAD false

typedef struct LinkLayer {
    bool is_receiver;
    unsigned int sequenceNumber;
    int serialFileDescriptor;
    struct termios oldtio;
    LinkLayerSettings *settings;
} LinkLayer;

void alarm_handler(int signo);
// Sender Functions
uint8_t* buildFrameHeader(uint8_t A, uint8_t C, uint16_t *frameSize, bool is_IframeHead);
uint8_t** buildUnstuffedFramesBodies(uint8_t *packet, size_t packetSize, size_t *nPayloadsAndFootersToProcess);
uint8_t* buildIFrame(uint8_t const *IFrameHeader, uint8_t IFrameHeaderSize, uint8_t const *UnstuffedIFrameBody, size_t *framedSize);
// Receiver functions
uint8_t* unstuffReceivedFrameHeaderAndCheckBcc(uint8_t const *IFrameHeader, uint8_t frameHeaderSize, uint8_t *unstuffedHeaderFrame, bool is_IframeHead);
uint8_t* unstuffReceivedIFrameBodyAndCheckBcc(uint8_t const *IFrameBody, uint16_t frameBodySize, uint8_t *UnstuffedIFrameBody);
// General functions
uint8_t generateBcc(const uint8_t *data, uint16_t size);
uint8_t generate_crc8(const uint8_t *data, uint16_t size);

LinkLayer LLayer;

// Tem de ser global por causa do llclose()
size_t leftOversSize = 0;
uint8_t *payloadsAndFootersLeftOver = NULL;

bool blocked = false;
bool alarmed = false;

int llinitialize(LinkLayerSettings *ptr, bool is_receiver) {
    if ( blocked ) {
        fprintf(stderr, "You can only initialize once\n");
        return -1;
    }

    if ( ptr == NULL ) {
        errno = EINVAL;
        return -1;
    }

    LLayer.settings = ptr;

    payloadsAndFootersLeftOver = (uint8_t *) malloc( sizeof(uint8_t) * LLayer.settings->payloadSize );
    LLayer.is_receiver = is_receiver;
    payloadsAndFootersLeftOver = NULL;

    blocked = true;
    return 0;
}

int llopen(void) {
    bool first, noEscYet;
    unsigned int tries = 0;
    struct termios newtio;
    uint8_t partOfFrame, state;
    uint8_t *frameToSend;
    uint16_t frameSize;
    uint8_t storeReceivedA, storeReceivedC;

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

    if ( LLayer.is_receiver ) frameToSend = buildFrameHeader(A_CSENDER_RRECEIVER, C_UA, &frameSize, IS_FRAMESUHEAD);
    else frameToSend = buildFrameHeader(A_CSENDER_RRECEIVER, C_SET, &frameSize, IS_FRAMESUHEAD);
    if ( frameToSend == NULL ) goto cleanUp;

    alarmed = false;
    while(tries < LLayer.settings->numAttempts) {

        if ( !LLayer.is_receiver ) {
            write(LLayer.serialFileDescriptor, frameToSend, frameSize);
            alarm(LLayer.settings->timeout);
        } else first = true;

        state = 0;

        noEscYet = true;
        while(!alarmed) {
            read(LLayer.serialFileDescriptor, &partOfFrame, 1);

            switch (state) {
                case 0:
                    if (partOfFrame == F)
                        if ( LLayer.is_receiver && first ) {
                            alarm(LLayer.settings->timeout);
                            first = false;
                        }
                        state = 1;
                    break;
                case 1:
                    if (partOfFrame == A_CSENDER_RRECEIVER) {
                        state = 2;
                        storeReceivedA = partOfFrame;
                    } else if (partOfFrame != F)
                        state = 0;
                    break;
                case 2:
                    if ( ((partOfFrame == C_SET) && LLayer.is_receiver) ||
                         ((partOfFrame == C_UA)  && !LLayer.is_receiver)
                       ) {
                        state = 3;
                        storeReceivedC = partOfFrame;
                    } else if (partOfFrame == F)
                        state = 1;
                    else
                        state = 0;
                    break;
                case 3:
                    if ( (partOfFrame == ESC) && noEscYet) {
                        state = 3;
                        noEscYet = false;
                    } else {
                        if ( noEscYet && (partOfFrame == (storeReceivedA ^ storeReceivedC)))
                            state = 4;
                        else if ( !noEscYet && (partOfFrame == ((storeReceivedA ^ storeReceivedC) ^ STUFFING_XOR_BYTE)))
                            state = 4;
                        else if (partOfFrame == F)
                            state = 1;
                        else
                            state = 0;
                        noEscYet = true;
                    }
                    break;
                case 4:
                    if (partOfFrame == F)
                        state = 5;
                    else
                        state = 0;
                    break;
                case 5:
                    if ( LLayer.is_receiver ) write(LLayer.serialFileDescriptor,frameToSend,frameSize);
                    free(frameToSend);
                    return 0;
                default:
                    goto cleanUp;
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

    size_t i;
    size_t nPayloadsAndFootersToProcess = 0;
    uint8_t **payloadsAndFooters = NULL;

    payloadsAndFooters = buildUnstuffedFramesBodies(packet,packetSize,&nPayloadsAndFootersToProcess);
    if ( errno != 0 ) return -1;

    // Construir as tramas de supervisão e não numeradas que vão ser precisas
    /*uint8_t* buildFrameHeader(uint8_t A, uint8_t C, uint16_t *frameSize, bool is_IframeHead);*/

    // Depois vem a máquina de estados
        // Ir construindo as tramas completas de informação à medida que vão sendo precisas
        // uint8_t* buildIFrame(uint8_t const *IFrameHeader, uint8_t IFrameHeaderSize, uint8_t const *UnstuffedIFrameBody, size_t *framedSize);

    // Limpar a tralha
    for ( i = 0; i < nPayloadsAndFootersToProcess; ++i) free(payloadsAndFooters[i]);
    free(payloadsAndFooters);

    return 0;
}

int llread(uint8_t *packet) {

    return 0;
}

int llclose(void) {

    bool noEscYet;
    unsigned int tries = 0;
    uint8_t partOfFrame, state;
    uint8_t *frameToSendDisc;
    uint8_t *frameToSendUA;
    uint16_t frameSizeDisc, frameSizeUA;
    uint8_t storeReceivedA, storeReceivedC;

    if ( !blocked ) {
        fprintf(stderr, "You have to llinitialize and llopen first\n");
        return -1;
    }

    if ( !LLayer.is_receiver ) {
        // SEND LeftOverFrame if there is one



        // END Connection in a proper way
        signal(SIGALRM, alarm_handler);

        frameToSendDisc = buildFrameHeader(A_CSENDER_RRECEIVER, C_DISC, &frameSizeDisc, IS_FRAMESUHEAD);
        if ( frameToSendDisc == NULL ) return -1;

        frameToSendUA = buildFrameHeader(A_CRECEIVER_RSENDER, C_UA, &frameSizeUA, IS_FRAMESUHEAD);
        if ( frameToSendUA == NULL ) {
            free(frameToSendDisc);
            return -1;
        }

        alarmed = false;
        while(tries < LLayer.settings->numAttempts) {

            write(LLayer.serialFileDescriptor, frameToSendDisc, frameSizeDisc);
            state = 0;

failedLastPhase:
            alarm(LLayer.settings->timeout);
            noEscYet = true;

            while(!alarmed) {

                read(LLayer.serialFileDescriptor, &partOfFrame, 1);

                switch (state) {
                    case 0:
                        if (partOfFrame == F)
                            state = 1;
                        break;
                    case 1:
                        if (partOfFrame == A_CSENDER_RRECEIVER) {
                            state = 2;
                            storeReceivedA = partOfFrame;
                        } else if (partOfFrame != F)
                            state = 0;
                        break;
                    case 2:
                        if ( partOfFrame == C_DISC ) {
                            state = 3;
                            storeReceivedC = partOfFrame;
                        } else if (partOfFrame == F)
                            state = 1;
                        else
                            state = 0;
                        break;
                    case 3:
                        if ( (partOfFrame == ESC) && noEscYet) {
                            state = 3;
                            noEscYet = false;
                        } else {
                            if ( noEscYet && (partOfFrame == (storeReceivedA ^ storeReceivedC)))
                                state = 4;
                            else if ( !noEscYet && (partOfFrame == ((storeReceivedA ^ storeReceivedC) ^ STUFFING_XOR_BYTE)))
                                state = 4;
                            else if (partOfFrame == F)
                                state = 1;
                            else
                                state = 0;
                            noEscYet = true;
                        }
                        break;
                    case 4:
                        if (partOfFrame == F)
                            state = 5;
                        else
                            state = 0;
                        break;
                    case 5:
                        alarm(0);
                        alarmed = false;
                        write(LLayer.serialFileDescriptor,frameToSendUA,frameSizeUA);
                        alarm(1);
                        while(!alarmed) {
                            if ( read(LLayer.serialFileDescriptor, &partOfFrame, 1) == 1 ) {
                                alarmed = false;
                                if ( partOfFrame == F ) state = 1;
                                else state = 0;
                                ++tries;
                                goto failedLastPhase;
                            }
                        }
                        goto SUCCESS;
                    default:
                        return -1;
                }
            }
            alarmed = false;
            ++tries;
        }
SUCCESS:
        free(frameToSendDisc);
        free(frameToSendUA);
    }
    sleep(3);
    free(payloadsAndFootersLeftOver);
    payloadsAndFootersLeftOver = NULL;
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

uint8_t* buildFrameHeader(uint8_t A, uint8_t C, uint16_t *frameSize, bool is_IframeHead) {
    uint8_t *tempHeader;
    uint8_t temp;
    uint8_t size = 6;

    if ( frameSize == NULL ) {
        errno = EINVAL;
        return NULL;
    }

    tempHeader = (uint8_t *) malloc( sizeof(uint8_t) * size);
    if ( tempHeader == NULL ) return NULL;
    tempHeader[0] = F;
    tempHeader[1] = A;
    tempHeader[2] = C;
    tempHeader[3] = generateBcc(tempHeader+1, 2);

    if ( tempHeader[3] == F  || tempHeader[3] == ESC ) {
        temp = tempHeader[3];
        tempHeader[3] = ESC;
        tempHeader[4] = temp ^ STUFFING_XOR_BYTE;
        temp = 5;
    } else temp = 4;

    if ( temp == 5 && is_IframeHead ) tempHeader = (uint8_t *) realloc(tempHeader,--size);
    else if ( temp == 4 && is_IframeHead) {
        size -= 2;
        tempHeader = (uint8_t *) realloc(tempHeader,size);
        if ( tempHeader == NULL ) {
            free(tempHeader);
            return NULL;
        }
    } else if ( !is_IframeHead ) {
        tempHeader[temp] = F;
        if ( temp == 4 ) {
            tempHeader = (uint8_t *) realloc(tempHeader,--size);
            if ( tempHeader == NULL ) {
                free(tempHeader);
                return NULL;
            }
        }
    }

    *frameSize = size;
    return tempHeader;
}

uint8_t** buildUnstuffedFramesBodies(uint8_t *packet, size_t packetSize, size_t *nPayloadsAndFootersToProcess) {

    uint8_t xorMe;
    uint8_t **payloadsAndFooters = NULL;
    size_t i, t;
    size_t temp;
    size_t nCompletePayloadsAndFooters;
    size_t fillUntil;

    errno = 0;

    if ( packet == NULL || nPayloadsAndFootersToProcess == NULL ) return NULL;

    if ( leftOversSize != 0 ) {
        if ( packetSize >= (LLayer.settings->payloadSize - leftOversSize) ) {
            payloadsAndFooters = (uint8_t **) malloc( sizeof(uint8_t *) * 1);
            if ( payloadsAndFooters == NULL ) return NULL;
            payloadsAndFooters[0] = (uint8_t *) malloc( sizeof(uint8_t) * LLayer.settings->payloadSize + 2 );
            if ( payloadsAndFooters[0] == NULL ) {
                free(payloadsAndFooters);
                return NULL;
            }
            fillUntil = LLayer.settings->payloadSize;
            for ( i = 0; i < leftOversSize; ++i) {
                payloadsAndFooters[0][i] = payloadsAndFootersLeftOver[i];
            }
        } else fillUntil = packetSize;

        xorMe = 0x00;
        for ( i = leftOversSize; i < fillUntil; ++i) {
            if ( fillUntil < LLayer.settings->payloadSize ) {
                payloadsAndFootersLeftOver[i] = *packet;
            } else {
                payloadsAndFooters[*nPayloadsAndFootersToProcess][i] = *packet;
                xorMe ^= *packet;
            }
            ++packet;
            --packetSize;
        }
        if ( i == LLayer.settings->payloadSize ) {
            payloadsAndFooters[0][i++] = xorMe;
            payloadsAndFooters[0][i] = F;
            *nPayloadsAndFootersToProcess = 1;
            leftOversSize = 0;
        } else {
            leftOversSize = i;
            *nPayloadsAndFootersToProcess = 0;
            return NULL;
        }
    }

    nCompletePayloadsAndFooters = packetSize / LLayer.settings->payloadSize;
    temp = *nPayloadsAndFootersToProcess + nCompletePayloadsAndFooters;
    payloadsAndFooters = (uint8_t **) realloc(payloadsAndFooters, temp);
    if ( payloadsAndFooters == NULL ) {
        free(*payloadsAndFooters);
        free(payloadsAndFooters);
        return NULL;
    }
    for( i = *nPayloadsAndFootersToProcess; i < temp; ++i) {
        payloadsAndFooters[i] = (uint8_t *) malloc( sizeof(uint8_t) * LLayer.settings->payloadSize + 2 );
        xorMe = 0x00;
        for ( t = 0; t < LLayer.settings->payloadSize; ++t) {
            payloadsAndFooters[i][t] = *packet;
            xorMe ^= *packet;
            --packetSize;
            ++packet;
        }
        payloadsAndFooters[i][t++] = xorMe;
        payloadsAndFooters[i][t] = F;
    }
    *nPayloadsAndFootersToProcess = i;

    while(packetSize--) {
        *payloadsAndFootersLeftOver = *packet;
        ++packet;
        ++leftOversSize;
        ++payloadsAndFootersLeftOver;
    }

    return payloadsAndFooters;
}

uint8_t* buildIFrame(uint8_t const *IFrameHeader, uint8_t IFrameHeaderSize, uint8_t const *UnstuffedIFrameBody, size_t *framedSize) {

    uint8_t *stuffedIFrame, temp;
    size_t framedTempSize;
    size_t i, n, t;

    if ( IFrameHeader == NULL || IFrameHeaderSize == 0 || UnstuffedIFrameBody == NULL || framedSize == NULL ) {
        errno = EINVAL;
        return NULL;
    }

    n = LLayer.settings->payloadSize;
    framedTempSize = IFrameHeaderSize + n + 2 + n/4;

    stuffedIFrame = (uint8_t *) malloc( sizeof(uint8_t) * framedTempSize );
    if ( stuffedIFrame == NULL ) return NULL;

    for( i = 0; i < IFrameHeaderSize; ++i) stuffedIFrame[i] = IFrameHeader[i];
    t = 0;
    while(n--) {
        if ( framedTempSize - t <= 10 ) {
            framedTempSize += 10;
            stuffedIFrame = (uint8_t *) realloc(stuffedIFrame,framedTempSize);
            if ( stuffedIFrame == NULL ) {
                free(stuffedIFrame);
                return NULL;
            }
        }
        if ( UnstuffedIFrameBody[t] == ESC || UnstuffedIFrameBody[t] == F ) {
            temp = UnstuffedIFrameBody[t];
            stuffedIFrame[i] = ESC;
            stuffedIFrame[++i] = temp ^ STUFFING_XOR_BYTE;
        } else stuffedIFrame[i] = UnstuffedIFrameBody[t];
        ++t;
        ++i;
    }

    stuffedIFrame = (uint8_t *) realloc(stuffedIFrame,i);
    if ( stuffedIFrame == NULL ) return NULL;

    *framedSize = i;

    return stuffedIFrame;
}

uint8_t* unstuffReceivedFrameHeaderAndCheckBcc(uint8_t const *IFrameHeader, uint8_t frameHeaderSize, uint8_t *unstuffedHeaderFrame, bool is_IframeHead) {

}

uint8_t* unstuffReceivedIFrameBodyAndCheckBcc(uint8_t const *IFrameBody, uint16_t frameBodySize, uint8_t *UnstuffedIFrameBody) {

}

uint8_t generateBcc(const uint8_t *data, uint16_t size) {

    size_t i;
    uint8_t bcc = 0x00;

    if ( data == NULL ) {
        errno = EINVAL;
        return 0;
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

