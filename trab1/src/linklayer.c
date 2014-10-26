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

char* buildFrameHeader(char A, char C, uint16_t *frameSize, bool is_IframeHead);

// Sender
char** buildUnstuffedFramesBodies(char *packet, size_t packetSize, size_t *nPayloadsAndFootersToProcess);
char* buildIFrame(char const *IFrameHeader, char IFrameHeaderSize, char const *UnstuffedIFrameBody, size_t *framedSize);

// Receiver
char* unstuffReceivedFrameHeaderAndCheckBcc(char const *FrameHeader, char frameHeaderSize, char *unstuffedHeaderFrameSize, bool is_IframeHead);
char* unstuffReceivedIFrameBodyAndCheckBcc(char const *IFrameBody, uint16_t IframeBodySize, char *UnstuffedIFrameBodySize);

// General
char generateBcc(const char *data, uint16_t size);
char generate_crc8(const char *data, uint16_t size);

LinkLayer LLayer;

// Tem de ser global por causa do llclose()
size_t leftOversSize = 0;
char *payloadsAndFootersLeftOver = NULL;

bool blocked = false;
bool alarmed = false;

uint16_t receiverState = 0;

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

    payloadsAndFootersLeftOver = (char *) malloc( sizeof(char) * LLayer.settings->payloadSize );
    LLayer.is_receiver = is_receiver;
    payloadsAndFootersLeftOver = NULL;

    blocked = true;
    return 0;
}

int llopen(void) {
    bool first, noEscYet;
    unsigned int tries = 0;
    struct termios newtio;
    char partOfFrame, state;
    char *frameToSend;
    uint16_t frameSize;
    char storeReceivedA, storeReceivedC;

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

    if ( LLayer.is_receiver ) {
        receiverState = 0;
        frameToSend = buildFrameHeader(A_CSENDER_RRECEIVER, C_UA, &frameSize, IS_FRAMESUHEAD);
    } else frameToSend = buildFrameHeader(A_CSENDER_RRECEIVER, C_SET, &frameSize, IS_FRAMESUHEAD);
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

int llwrite(char *packet, size_t packetSize) {

    size_t i;
    size_t nPayloadsAndFootersToProcess = 0;
    char **payloadsAndFooters = NULL;

    payloadsAndFooters = buildUnstuffedFramesBodies(packet,packetSize,&nPayloadsAndFootersToProcess);
    if ( errno != 0 ) return -1;

    // Construir as tramas de supervisão e não numeradas que vão ser precisas
    /*char* buildFrameHeader(char A, char C, uint16_t *frameSize, bool is_IframeHead);*/

    // Depois vem a máquina de estados
        // Ir construindo as tramas completas de informação à medida que vão sendo precisas
        // char* buildIFrame(char const *IFrameHeader, char IFrameHeaderSize, char const *UnstuffedIFrameBody, size_t *framedSize);

    // Limpar a tralha
    for ( i = 0; i < nPayloadsAndFootersToProcess; ++i) free(payloadsAndFooters[i]);
    free(payloadsAndFooters);

    return 0;
}

// retorna NULL && *error = -1,  em caso de erro
// retorna NULL %% *error = 0, se receber disconnect e depois um UA para a applayer depois fazer llclose
char* llread(uint16_t *packetSize, int *error) {

    if ( packetSize == NULL || error == NULL) {
        errno = EINVAL;
        return NULL;
    }

    // Usar a variável receiverState para o estado ficar guardado entre chamadas, pq é global
    // Fazer unstuffing e cacular BCC
    // Se receber uma trama de informação válida, preenche o tamanho *packetSize, retorna o endereço do pacote (tem de estar unstuffed!)

    *error = -1;
    return NULL;
}

int llclose(void) {

    bool noEscYet;
    unsigned int tries = 0;
    char partOfFrame, state;
    char *frameToSendDisc;
    char *frameToSendUA;
    uint16_t frameSizeDisc, frameSizeUA;
    char storeReceivedA, storeReceivedC;

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

char* buildFrameHeader(char A, char C, uint16_t *frameSize, bool is_IframeHead) {
    char *tempHeader;
    char temp;
    unsigned char size = 6;

    if ( frameSize == NULL ) {
        errno = EINVAL;
        return NULL;
    }

    tempHeader = (char *) malloc( sizeof(char) * size);
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

    if ( temp == 5 && is_IframeHead ) tempHeader = (char *) realloc(tempHeader,--size);
    else if ( temp == 4 && is_IframeHead) {
        size -= 2;
        tempHeader = (char *) realloc(tempHeader,size);
        if ( tempHeader == NULL ) {
            free(tempHeader);
            return NULL;
        }
    } else if ( !is_IframeHead ) {
        tempHeader[(unsigned char)temp] = F;
        if ( temp == 4 ) {
            tempHeader = (char *) realloc(tempHeader,--size);
            if ( tempHeader == NULL ) {
                free(tempHeader);
                return NULL;
            }
        }
    }

    *frameSize = size;
    return tempHeader;
}

char** buildUnstuffedFramesBodies(char *packet, size_t nBytes, size_t *nPayloadsAndFootersToProcess) {

    char xorMe;
    char **payloadsAndFooters = NULL;
    size_t i, t;
    size_t temp;
    size_t nCompletePayloadsAndFooters;
    size_t fillUntil;

    errno = 0;

    if ( packet == NULL || nPayloadsAndFootersToProcess == NULL ) return NULL;

    if ( leftOversSize != 0 ) {
        if ( nBytes >= (LLayer.settings->payloadSize - leftOversSize) ) {
            payloadsAndFooters = (char **) malloc( sizeof(char *) * 1);
            if ( payloadsAndFooters == NULL ) return NULL;
            payloadsAndFooters[0] = (char *) malloc( sizeof(char) * LLayer.settings->payloadSize + 2 );
            if ( payloadsAndFooters[0] == NULL ) {
                free(payloadsAndFooters);
                return NULL;
            }
            fillUntil = LLayer.settings->payloadSize;
            for ( i = 0; i < leftOversSize; ++i) {
                payloadsAndFooters[0][i] = payloadsAndFootersLeftOver[i];
            }
        } else fillUntil = nBytes;

        xorMe = 0x00;
        for ( i = leftOversSize; i < fillUntil; ++i) {
            if ( fillUntil < LLayer.settings->payloadSize ) {
                payloadsAndFootersLeftOver[i] = *packet;
            } else {
                payloadsAndFooters[*nPayloadsAndFootersToProcess][i] = *packet;
                xorMe ^= *packet;
            }
            ++packet;
            --nBytes;
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

    nCompletePayloadsAndFooters = nBytes / LLayer.settings->payloadSize;
    temp = *nPayloadsAndFootersToProcess + nCompletePayloadsAndFooters;
    payloadsAndFooters = (char **) realloc(payloadsAndFooters, temp);
    if ( payloadsAndFooters == NULL ) {
        free(*payloadsAndFooters);
        free(payloadsAndFooters);
        return NULL;
    }
    for( i = *nPayloadsAndFootersToProcess; i < temp; ++i) {
        payloadsAndFooters[i] = (char *) malloc( sizeof(char) * LLayer.settings->payloadSize + 2 );
        xorMe = 0x00;
        for ( t = 0; t < LLayer.settings->payloadSize; ++t) {
            payloadsAndFooters[i][t] = *packet;
            xorMe ^= *packet;
            --nBytes;
            ++packet;
        }
        payloadsAndFooters[i][t++] = xorMe;
        payloadsAndFooters[i][t] = F;
    }
    *nPayloadsAndFootersToProcess = i;

    while(nBytes--) {
        *payloadsAndFootersLeftOver = *packet;
        ++packet;
        ++leftOversSize;
        ++payloadsAndFootersLeftOver;
    }

    return payloadsAndFooters;
}

char* buildIFrame(char const *IFrameHeader, char IFrameHeaderSize, char const *UnstuffedIFrameBody, size_t *framedSize) {

    char *stuffedIFrame, temp;
    size_t framedTempSize;
    size_t i, n, t;

    if ( IFrameHeader == NULL || IFrameHeaderSize == 0 || UnstuffedIFrameBody == NULL || framedSize == NULL ) {
        errno = EINVAL;
        return NULL;
    }

    n = LLayer.settings->payloadSize;
    framedTempSize = IFrameHeaderSize + n + 2 + n/4;

    stuffedIFrame = (char *) malloc( sizeof(char) * framedTempSize );
    if ( stuffedIFrame == NULL ) return NULL;

    for( i = 0; i < IFrameHeaderSize; ++i) stuffedIFrame[i] = IFrameHeader[i];
    t = 0;
    while(n--) {
        if ( framedTempSize - t <= 10 ) {
            framedTempSize += 10;
            stuffedIFrame = (char *) realloc(stuffedIFrame,framedTempSize);
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

    stuffedIFrame = (char *) realloc(stuffedIFrame,i);
    if ( stuffedIFrame == NULL ) return NULL;

    *framedSize = i;

    return stuffedIFrame;
}

char* unstuffReceivedFrameHeaderAndCheckBcc(char const *FrameHeader, char frameHeaderSize, char *unstuffedHeaderFrameSize, bool is_IframeHead) {

    return NULL;
}

char* unstuffReceivedIFrameBodyAndCheckBcc(char const *IFrameBody, uint16_t IframeBodySize, char *UnstuffedIFrameBodySize) {

    return NULL;
}

char generateBcc(const char *data, uint16_t size) {

    size_t i;
    char bcc = 0x00;

    if ( data == NULL ) {
        errno = EINVAL;
        return 0;
    }

    for( i = 0; i < size; ++i) {
        bcc ^= data[i];
    }

    return bcc;
}

char generate_crc8(const char *data, uint16_t size) {

    char R = 0;
    char bitsRead = 0;

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

