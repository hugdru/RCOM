#define _XOPEN_SOURCE
#include "linklayer.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>

/**
 * Defines
 */

#define F 0x7E
#define A_CSENDER_RRECEIVER 0x03
#define A_CRECEIVER_RSENDER 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x11
#define C_RR_RAW 0x05
#define C_REJ_RAW 0x01
#define C_I_RAW 0x00
#define ESC 0x7D
#define STUFFING_XOR_BYTE 0x20

typedef struct {
    bool is_receiver;
    int sequenceNumber;
    int serialFileDescriptor;
    struct termios oldtio;
    LinkLayerSettings *settings;

    uint8_t * frame;
    size_t frameLength;
} LinkLayer;

typedef enum {
    START, F_RCV, A_RCV, C_RCV, BCC_OK, RCV_I
} State;

/**
 * Function Prototypes
 */

static void alarm_handler(int signo);
static void print_frame(uint8_t * frame, size_t size);
static void print_cmd(uint8_t C);
static uint8_t* buildFrameHeader(uint8_t A, uint8_t C, size_t * headerSize,
        bool is_IframeHead);
static uint8_t * buildIFrame(uint8_t * packet, size_t packetSize,
        size_t * stuffedFrameSize);
static uint8_t generateBcc(const uint8_t * data, size_t size);
static uint8_t * stuff(uint8_t * packet, size_t size, size_t * stuffedSize);
static int changeSequenceNumber(void);
static bool isCMD(uint8_t ch);
static bool isCMDI(uint8_t ch);
static bool readCMD(uint8_t * C);

/**
 * Global Variables
 */

bool blockedSet = false; // Usado no LLread -> Não usar para mais nada
bool blocked = false; // Usado para o llinitialize, llopen e llclose -> Não usar para mais nada
bool alarmed = false;
struct sigaction *new_act = NULL;

LinkLayer linkLayer;

/**
 * LinkLayer API
 */

int llinitialize(LinkLayerSettings *ptr, bool is_receiver) {


    if (blocked) {
        fprintf(stderr, "You can only initialize once\n");
        return -1;
    }

    if (ptr == NULL) {
        errno = EINVAL;
        return -1;
    }

    linkLayer.settings = ptr;
    linkLayer.is_receiver = is_receiver;
    linkLayer.sequenceNumber = 0;

    if ( new_act == NULL ) {
        new_act = (struct sigaction *) malloc(sizeof(struct sigaction));
        if ( new_act == NULL ) {
            errno = ENOMEM;
            return -1;
        }
        new_act->sa_handler = alarm_handler;
        sigemptyset(&(new_act->sa_mask));
        new_act->sa_flags = 0;
        if ( sigaction(SIGALRM, new_act, 0 ) == -1 ) {
            new_act = NULL;
            free(new_act);
            return -1;
        }
    }

    linkLayer.frame = (uint8_t *) malloc(linkLayer.settings->payloadSize + 6);
    linkLayer.frameLength = 0;

    blocked = true;
    return 0;
}

int llopen(void) {
    unsigned int tries = 0;
    struct termios newtio;

    if (!blocked) {
        fprintf(stderr, "You have to llinitialize first\n");
        return -1;
    }

    /*
     Open serial port device for reading and writing and not as controlling tty
     because we don't want to get killed if linenoise sends CTRL-C.
     */
    linkLayer.serialFileDescriptor = open(linkLayer.settings->port,
    O_RDWR | O_NOCTTY);

    if (linkLayer.serialFileDescriptor < 0) {
        perror(linkLayer.settings->port);
        return -1;
    }

    if (tcgetattr(linkLayer.serialFileDescriptor, &(linkLayer.oldtio)) < 0) { /* save current port settings */
        perror("tcgetattr");
        close(linkLayer.serialFileDescriptor);
        return -1;
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = linkLayer.settings->baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 1; /* inter-character timer unused */
    newtio.c_cc[VMIN] = 0; /* blocking read until 0 chars received */

    /*
     VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
     leitura do(s) próximo(s) caracter(es)
     */
    tcflush(linkLayer.serialFileDescriptor, TCIOFLUSH);

    if (tcsetattr(linkLayer.serialFileDescriptor, TCSANOW, &newtio) == -1) {
        perror("Failed to set new settings for port, tcsetattr");
        close(linkLayer.serialFileDescriptor);
        return -1;
    }

    fprintf(stderr, "New termios structure set\n");

    // Esta funcao esta deprecated e estava a dar problemas acontecia o seguinte
    // 'There are few reasons and most important is that the original Unix implementation would reset the signal handler to it's default value after signal is received'
    // From http://www.linuxprogrammingblog.com/all-about-linux-signals?page=show
    // signal(SIGALRM, alarm_handler); // Sets function alarm_handler as the handler of alarm signals

    uint8_t C;
    uint8_t * cmd;
    size_t cmdSize;
    int received;
    ssize_t res;

    if (linkLayer.is_receiver)
        cmd = buildFrameHeader(A_CSENDER_RRECEIVER, C_UA, &cmdSize, false);
    else
        cmd = buildFrameHeader(A_CSENDER_RRECEIVER, C_SET, &cmdSize, false);

    while (tries < linkLayer.settings->numAttempts) {
        alarmed = false;
        if (linkLayer.is_receiver) {
            alarm(linkLayer.settings->timeout);
            received = readCMD(&C);
            if (received && C == C_SET) {
                res = write(linkLayer.serialFileDescriptor, cmd, cmdSize); // o que fazer com res?
                return 0;
            }
        } else {
            res = write(linkLayer.serialFileDescriptor, cmd, cmdSize); // o que fazer com res?
            alarm(linkLayer.settings->timeout);
            received = readCMD(&C);
            if (received && C == C_UA)
                return 0;
        }

        tries++;
    }

    //Failed
    if (tcsetattr(linkLayer.serialFileDescriptor, TCSANOW, &(linkLayer.oldtio))
            < 0) { /* Restores old port settings */
        perror("tcsetattr");
        exit(1);
    }

    close(linkLayer.serialFileDescriptor);
    return -1;
}

int llwrite(uint8_t *packet, size_t packetSize) {
    size_t stuffedFrameSize;
    uint8_t * stuffedFrame = buildIFrame(packet, packetSize, &stuffedFrameSize);

    unsigned int tries = 0;
    bool received, done = false;
    ssize_t res;
    uint8_t C;

    while (tries < linkLayer.settings->numAttempts) {
        alarmed = false;
        received = false;

        fprintf(stderr, "Sending frame          Tries: %d\n", tries);
        res = write(linkLayer.serialFileDescriptor, stuffedFrame,
                stuffedFrameSize);
        alarm(linkLayer.settings->timeout);

        received = readCMD(&C);

        fprintf(stderr, "Receive: %d\n", received);
        if (received) {
            tries = 0;
            if ( C == (C_RR_RAW | (linkLayer.sequenceNumber << 7)) ) { // RR Errado
            } else if ( C == (C_RR_RAW | (changeSequenceNumber() << 7)) ) { // RR Certo
                linkLayer.sequenceNumber = changeSequenceNumber();
                return 0;
            } else if ( C == (C_REJ_RAW | (linkLayer.sequenceNumber << 7)) ) { //REJ Certo
            } else if ( C == (C_REJ_RAW | (linkLayer.sequenceNumber << 7)) ) {//REJ Errado
            } else { //Recebeu um comando que não esperava
            }
        }

        tries++;
    }

    fprintf(stderr, "LLWRITE\n");
    return 1;
}

// errno != 0 em caso de erro
// retorna NULL e errno = 0, se receber disconnect e depois um UA para a applayer depois fazer llclose
// retorna endereço do pacote, *packetSize tamanho do pacote recebido
uint8_t* llread(size_t *payloadSize) {

    unsigned int tries = 0;
    bool received;
    ssize_t res;
    uint8_t C;
    uint8_t previousC;
    uint8_t *payloadToReturn;
    size_t i, tempSize;

    size_t uaCmdSize;
    uint8_t *uaCmd = buildFrameHeader(A_CSENDER_RRECEIVER, C_UA, &uaCmdSize, false);
    if ( uaCmd == NULL ) {
        errno = ENOMEM;
        return NULL;
    }
    size_t discCmdSize;
    uint8_t *discCmd = buildFrameHeader(A_CSENDER_RRECEIVER, C_DISC, &discCmdSize, false);
    if ( discCmd == NULL ) {
        free(uaCmd);
        errno = ENOMEM;
        return NULL;
    }
    size_t rr0CmdSize;
    uint8_t *rr0Cmd = buildFrameHeader(A_CSENDER_RRECEIVER, C_RR_RAW, &rr0CmdSize, false);
    if ( rr0Cmd == NULL ) {
        free(uaCmd);
        errno = ENOMEM;
        return NULL;
    }
    size_t rr1CmdSize;
    uint8_t *rr1Cmd = buildFrameHeader(A_CSENDER_RRECEIVER, C_RR_RAW | 0x80, &rr1CmdSize, false);
    if ( rr1Cmd == NULL ) {
        free(uaCmd);
        free(rr0Cmd);
        errno = ENOMEM;
        return NULL;
    }
    size_t rej0CmdSize;
    uint8_t *rej0Cmd = buildFrameHeader(A_CSENDER_RRECEIVER, C_REJ_RAW, &rej0CmdSize, false);
    if ( rej0Cmd == NULL ) {
        free(uaCmd);
        free(rr0Cmd);
        free(rr1Cmd);
        errno = ENOMEM;
        return NULL;
    }
    size_t rej1CmdSize;
    uint8_t *rej1Cmd = buildFrameHeader(A_CSENDER_RRECEIVER, C_REJ_RAW | 0x80, &rej1CmdSize, false);
    if ( rej1Cmd == NULL ) {
        free(uaCmd);
        free(rr0Cmd);
        free(rr1Cmd);
        free(rej0Cmd);
        errno = ENOMEM;
        return NULL;
    }

    errno = 0;

    while (tries < linkLayer.settings->numAttempts) {
        fprintf(stderr, "Receiving frame\n");
        alarmed = false;
        alarm(linkLayer.settings->timeout);
        received = readCMD(&C);
        if (tries == 0) previousC = C;

        if (received) {
            if (!blockedSet) {
                if ( C == C_SET ) { // Transmitter não recebeu bem o UA
                    res = write(linkLayer.serialFileDescriptor, uaCmd, uaCmdSize); // o que fazer com res?
                } else if ( !isCMDI(C) ) { // Se não for uma trama de informação
                    fprintf(stderr, "Garbage command received"); // O ruído pode 'construir' uma trama sem erros não esperada!
                    // Como não sei se vou receber um set ou uma trama de informação não faço nada
                } else blockedSet = true;
            }
            if (blockedSet) {
                /* Trata do caso do header da tramaI estiver certo mas o body estiver errado */
                if ( (C == (C_I_RAW | (linkLayer.sequenceNumber << 6))) && (linkLayer.frame[linkLayer.frameLength-1] != F) ) {
                    // rej
                    fprintf(stderr, "Cabeça da trama I boa, resto mau, mesma sequência -> rej\n");
                    if (linkLayer.sequenceNumber)
                        res = write(linkLayer.serialFileDescriptor, rej1Cmd, rej1CmdSize);
                    else
                        res = write(linkLayer.serialFileDescriptor, rej0Cmd, rej0CmdSize);
                } else if ( (C == (C_I_RAW | (changeSequenceNumber() << 6))) && (linkLayer.frame[linkLayer.frameLength-1] != F) ) {
                    // rr
                    fprintf(stderr, "Cabeça da trama I boa, resto mau, sequência diferente -> rr\n");
                    if (linkLayer.sequenceNumber)
                        res = write(linkLayer.serialFileDescriptor, rr1Cmd, rr1CmdSize);
                    else
                        res = write(linkLayer.serialFileDescriptor, rr0Cmd, rr0CmdSize);
                /* Fim de caso especial header certo mas body errado */
                } else if ( C == (C_I_RAW | (linkLayer.sequenceNumber << 6)) ) { // Trama I esperada
                    fprintf(stderr, "Trama I esperada\n");
                    tempSize = linkLayer.frameLength - 6;
                    payloadToReturn = (uint8_t *) malloc( sizeof(uint8_t) * tempSize );
                    if ( payloadToReturn == NULL ) {
                        free(uaCmd);
                        free(rr0Cmd);
                        free(rr1Cmd);
                        free(rej0Cmd);
                        free(rej1Cmd);
                        errno = ENOMEM;
                        return NULL;
                    }
                    for (i = 0; i < tempSize; ++i) {
                        payloadToReturn[i] = linkLayer.frame[i+4];
                    }
                    *payloadSize = tempSize;
                    linkLayer.sequenceNumber = changeSequenceNumber();
                    if ( linkLayer.sequenceNumber == 0 ) res = write(linkLayer.serialFileDescriptor, rr0Cmd, rr0CmdSize);
                    else res = write(linkLayer.serialFileDescriptor, rr1Cmd, rr1CmdSize);
                    return payloadToReturn;
                } else if ( C == (C_I_RAW | (changeSequenceNumber() << 6)) ) { // Trama I duplicada, emissor nao recebeu a confirmação a tempo ou a confirmação foi perdida na rede
                    if ( linkLayer.sequenceNumber == 0 ) 
                        res = write(linkLayer.serialFileDescriptor, rr0Cmd, rr0CmdSize);
                    else 
                        res = write(linkLayer.serialFileDescriptor, rr1Cmd, rr1CmdSize);
                    fprintf(stderr, "Trama duplicada");
                } else if ( C == C_DISC ) { // Transmitter já enviou tudo
                    fprintf(stderr, "LLread received valid disconnect\n");
                    free(uaCmd);
                    free(rr0Cmd);
                    free(rr1Cmd);
                    free(rej0Cmd);
                    free(rej1Cmd);
                    return NULL;
                } else { // Recebeu uma trama de supervisão ou não numerada válida mas não esperada, ruído tramado!
                    if ( linkLayer.sequenceNumber == 0 ) res = write(linkLayer.serialFileDescriptor, rr0Cmd, rr0CmdSize);
                    else res = write(linkLayer.serialFileDescriptor, rr1Cmd, rr1CmdSize);
                    fprintf(stderr, "Não esperava esta trama");
                }
            }
        }
        if (previousC == C || !received) ++tries;
        else tries = 0;
        previousC = C;
    }

    free(uaCmd);
    free(rr0Cmd);
    free(rr1Cmd);
    free(rej0Cmd);
    free(rej1Cmd);
    errno = ECONNABORTED;
    return NULL;
}

int llclose(void) {
    unsigned int tries = 0;
    static bool success = false;

    if (!blocked) {
        fprintf(stderr, "You have to llinitialize and llopen first\n");
        return -1;
    }

    size_t DISCsize, UAsize;
    uint8_t * DISC = buildFrameHeader(A_CSENDER_RRECEIVER, C_DISC, &DISCsize,
            false);
    uint8_t * UA = buildFrameHeader(A_CSENDER_RRECEIVER, C_UA, &UAsize, false);

    uint8_t C;
    int received;
    ssize_t res;

    if ( !success ) {
        while (tries < linkLayer.settings->numAttempts) {
            alarmed = false;
            // Esta parte é para mudar, só depois de o appLayer estar feito
            if (linkLayer.is_receiver) {
                res = write(linkLayer.serialFileDescriptor, DISC, DISCsize);
                alarm(linkLayer.settings->timeout);
                received = readCMD(&C);
                if (received && C == C_UA) {
                    success = true;
                    goto cleanSerial;
                }
            } else {
                res = write(linkLayer.serialFileDescriptor, DISC, DISCsize);
                alarm(linkLayer.settings->timeout);
                received = readCMD(&C);
                if (received && C == C_DISC) {
                    res = write(linkLayer.serialFileDescriptor, UA, UAsize); //o que fazer com res
                    success = true;
                    goto cleanSerial;
                }
            }
            tries++;
        }
    }

    cleanSerial:

    blockedSet = false;

    if ( linkLayer.frame != NULL ) {
        free(linkLayer.frame);
        linkLayer.frame = NULL;
    }

    if (success) {
        if (tcsetattr(linkLayer.serialFileDescriptor, TCSANOW, &(linkLayer.oldtio)) < 0) {
            perror("tcsetattr");
            return -1;
        }
        if ( close(linkLayer.serialFileDescriptor) != 0 ) return -1;
        blocked = false;
        success = false;
        free(new_act);
        new_act = NULL;
        return 0;
    } else return -1;
}

/**
 * More Functions
 */

static void alarm_handler(int signo) {
    alarmed = true;
    fprintf(stderr, "alarm\n");
}

static int changeSequenceNumber(void) {
    return 1 - linkLayer.sequenceNumber;
}

static void print_frame(uint8_t * frame, size_t size) {
    size_t i;
    for (i = 0; i < size; i++)
        fprintf(stderr, "%X ", frame[i]);
    fprintf(stderr, "\n");
}

static void print_cmd(uint8_t C) {
    switch (C) {
    case C_SET:
        fprintf(stderr, "C_SET");
        break;
    case C_UA:
        fprintf(stderr, "C_UA");
        break;
    case C_DISC:
        fprintf(stderr, "C_DISC");
        break;
    case C_RR_RAW:
        fprintf(stderr, "C_RR_0");
        break;
    case (C_RR_RAW | 0x80):
        fprintf(stderr, "C_RR_1");
        break;
    case C_REJ_RAW:
        fprintf(stderr, "C_REJ_0");
        break;
    case (C_REJ_RAW | 0x80):
        fprintf(stderr, "C_REJ_1");
        break;
    case C_I_RAW:
        fprintf(stderr, "C_I_0");
        break;
    case (C_I_RAW | 0x40):
        fprintf(stderr, "C_I_1");
        break;
    default:
        break;
    }
}

static bool isCMD(uint8_t ch) {
    return (ch == C_SET || ch == C_UA || ch == C_DISC || (ch & 0x7F) == C_RR_RAW
            || (ch & 0x7F) == C_REJ_RAW);
}

static bool isCMDI(uint8_t ch) {
    return (ch & 0xBF) == C_I_RAW;
}

static bool readCMD(uint8_t * C) {
    ssize_t res;
    uint8_t ch, BCC1 = 0x00, BCC2 = 0x00, temp;
    bool stuffing = false;
    State state = START;

    linkLayer.frameLength = 0;

    // Não sei se isto tem um problema, imagina o seguinte:
    // O emissor envia uma trama com erro, como ele espera até receber
    // a resposta (e como não envia nada enquanto) não vai haver nada para ler
    // por isso ele vai esperar nesta função algum tempo desnecessariamente até tocar o alarm.
    // Uma maneira de resolver era adicionar um parâmetro que indicava o comando a reenviar em caso de erro
    // Não vou fazer isto por enquanto pq muda o prototipo e não quero estar a confundir as coisas
    while (!alarmed) {
        res = read(linkLayer.serialFileDescriptor, &ch, 1);

        if ( res == 1 ) {
            fprintf(stderr, "State: %d     Res: %lu\n", state, res);
            fprintf(stderr, "Char: %c\n", ch);
            switch (state) {
            case START:
                if (ch == F)
                    state = F_RCV;
                break;
            case F_RCV:
                if (ch == A_CSENDER_RRECEIVER) {
                    state = A_RCV;
                    BCC1 = ch;
                } else if (ch != F)
                    state = START;
                break;
            case A_RCV:
                if (ch == F)
                    state = F_RCV;
                else {
                    if (isCMD(ch) || isCMDI(ch)) {
                        BCC1 ^= ch;
                        *C = ch;
                        state = C_RCV;
                    } else
                        state = START;
                }
                break;
            case C_RCV:
                if (stuffing) { //Destuffing in run-time
                    stuffing = false;
                    if ((ch ^ STUFFING_XOR_BYTE) == BCC1) {
                        state = BCC_OK;
                    }
                } else if (ch == ESC) {
                    stuffing = true;
                } else if (ch == BCC1) {
                    state = BCC_OK;
                } else if (ch == F)
                    state = F_RCV;
                else
                    state = START;
                break;
            case BCC_OK:
                if (ch == F && isCMD(*C)) {
                    fprintf(stderr, "Received CMD: ");
                    print_cmd(*C);
                    fprintf(stderr, "\n");
                    return true;
                } else if (isCMDI(*C) && (ch != F) && linkLayer.is_receiver) {
                    linkLayer.frame[linkLayer.frameLength++] = F;
                    linkLayer.frame[linkLayer.frameLength++] = A_CSENDER_RRECEIVER;
                    linkLayer.frame[linkLayer.frameLength++] = *C;
                    linkLayer.frame[linkLayer.frameLength++] = BCC1;
                    if ( ch == ESC ) {
                        stuffing = true;
                        BCC2 = 0x00;
                    } else {
                        linkLayer.frame[linkLayer.frameLength++] = ch;
                        BCC2 = ch;
                    }
                    state = RCV_I;
                    fprintf(stderr, "Receiving Frame I\n");
                } else
                    state = START;
                break;
            case RCV_I:
                if (linkLayer.frameLength >= (linkLayer.settings->payloadSize + 6)) {
                    fprintf(stderr, "This payload is invalid cause it exceeds the max number of bytes\n");
                    linkLayer.frameLength = 0;
                    if (ch == F) state = F_RCV;
                    else state = START;
                } else if ( (ch == F) && stuffing ) {
                    state = F_RCV;
                    linkLayer.frameLength = 0;
                    stuffing = false;
                } else if (ch == F) {
                    fprintf(stderr, "Finishing up Iframe, F received\n");
                    BCC2 ^= linkLayer.frame[linkLayer.frameLength - 1]; // Reverter, pois o ultimo é o BCC2
                    fprintf(stderr, "BBC2: %X, BBC2 in frame: %X\n", BCC2, linkLayer.frame[linkLayer.frameLength-1]);
                    if (BCC2 == linkLayer.frame[linkLayer.frameLength - 1]) {
                        linkLayer.frame[linkLayer.frameLength++] = ch;
                        printf("Received Frame I, Length: %lu",
                                linkLayer.frameLength);
                    }
                    // Uma vez que tem o cabeçalho da header válido
                    // Rej e RR, fora ele verifica se o último elemento é F ou não
                    return true;
                } else if (stuffing) {  //Destuffing in run-time
                    stuffing = false;
                    temp = ch ^ STUFFING_XOR_BYTE;
                    linkLayer.frame[linkLayer.frameLength++] = temp;
                    BCC2 ^= temp;
                } else if (ch == ESC) {
                    stuffing = true;
                } else {
                    BCC2 ^= ch;
                    linkLayer.frame[linkLayer.frameLength++] = ch;
                }
                break;
            default:
                return false;
                break;
            }
        }
    }
    return false;
}

static uint8_t * stuff(uint8_t * packet, size_t size, size_t * stuffedSize) {
    *stuffedSize = size;

    uint8_t BCC = generateBcc(packet, size);

    size_t i;
    for (i = 0; i < size; i++)
        if (packet[i] == F || packet[i] == ESC)
            (*stuffedSize)++;

    (*stuffedSize)++;
    if(BCC == ESC || BCC == F)
        (*stuffedSize)++;

    uint8_t * stuffed = (uint8_t *) malloc(*stuffedSize);

    size_t j = 0;
    for (i = 0; i < size; i++) {
        if (packet[i] == ESC || packet[i] == F) {
            stuffed[j] = ESC;
            j++;
            stuffed[j] = STUFFING_XOR_BYTE ^ packet[i];
        } else
            stuffed[j] = packet[i];
        j++;
    }

    if(BCC == ESC || BCC == F) {
        stuffed[*stuffedSize-2] = ESC;
        stuffed[*stuffedSize-1] = BCC^STUFFING_XOR_BYTE;
    }
    stuffed[*stuffedSize-1] = BCC;

    return stuffed;
}

static uint8_t* buildFrameHeader(uint8_t A, uint8_t C, size_t *headerSize,
        bool is_IframeHead) {
    size_t size = 4;
    bool stuffed = false;

    uint8_t BCC = A ^ C;
    if (BCC == F || BCC == ESC) {
        size++;
        stuffed = true;
        BCC ^= STUFFING_XOR_BYTE;
    }

    if (!is_IframeHead)
        size++;

    uint8_t *header = (uint8_t *) malloc(size);

    uint8_t i = 0;
    header[i++] = F;
    header[i++] = A;
    header[i++] = C;

    if (stuffed)
        header[i++] = ESC;

    header[i++] = BCC;

    if (!is_IframeHead)
        header[i] = F;

    *headerSize = size;
    return header;
}

static uint8_t * buildIFrame(uint8_t * packet, size_t packetSize,
        size_t * stuffedFrameSize) {
    size_t stuffedHeaderSize;
    uint8_t * stuffedHeader = buildFrameHeader(A_CSENDER_RRECEIVER,
            (linkLayer.sequenceNumber << 6), &stuffedHeaderSize, true);

    size_t stuffedPacketSize;
    uint8_t * stuffedPacket = stuff(packet, packetSize, &stuffedPacketSize);

    *stuffedFrameSize = stuffedHeaderSize + stuffedPacketSize + 1; //Header + Packet + F
    uint8_t * stuffedFrame = (uint8_t *) malloc(*stuffedFrameSize);

    memcpy(stuffedFrame, stuffedHeader, stuffedHeaderSize);
    memcpy(stuffedFrame + stuffedHeaderSize, stuffedPacket, stuffedPacketSize);
    stuffedFrame[*stuffedFrameSize - 1] = F;

    return stuffedFrame;
}

static uint8_t generateBcc(const uint8_t * data, size_t size) {
    size_t i;
    uint8_t bcc = 0x00;

    if (data == NULL) {
        errno = EINVAL;
        return 0;
    }

    for (i = 0; i < size; ++i)
        bcc ^= data[i];

    return bcc;
}

