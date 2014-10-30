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
#define ESC 0x7D
#define STUFFING_XOR_BYTE 0x20

typedef struct LinkLayer {
    bool is_receiver;
    unsigned int sequenceNumber;
    int serialFileDescriptor;
    struct termios oldtio;
    LinkLayerSettings *settings;
} LinkLayer;


/**
 * Function Prototypes
 */

static void alarm_handler(int signo);
static void print_frame(uint8_t * frame, size_t size);
static uint8_t* buildFrameHeader(uint8_t A, uint8_t C, size_t * headerSize, bool is_IframeHead);
static uint8_t * buildIFrame(uint8_t * packet, size_t packetSize, size_t * stuffedFrameSize);
static uint8_t generateBcc(const uint8_t * data, size_t size);
static uint8_t * stuff(uint8_t * packet, size_t size, size_t * stuffedSize);
static int changeSequenceNumber();
static bool isKnownC(uint8_t ch);
static uint8_t readCMD();


/**
 * Global Variables
 */

bool blocked = false;
bool alarmed = false;
uint8_t receiverState = 0;
LinkLayer linkLayer;


/**
 * LinkLayer API
 */

int llinitialize(LinkLayerSettings *ptr, bool is_receiver) {
    if ( blocked ) {
        fprintf(stderr, "You can only initialize once\n");
        return -1;
    }

    if ( ptr == NULL ) {
        errno = EINVAL;
        return -1;
    }

    linkLayer.settings = ptr;
    linkLayer.is_receiver = is_receiver;

    blocked = true;
    return 0;
}

int llopen(void) {
    unsigned int tries = 0;
    struct termios newtio;

    if ( !blocked ) {
        fprintf(stderr, "You have to llinitialize first\n");
        return -1;
    }

  /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
  */
    linkLayer.serialFileDescriptor = open(linkLayer.settings->port, O_RDWR | O_NOCTTY);

    if ( linkLayer.serialFileDescriptor < 0 ) {
        perror(linkLayer.settings->port);
        return -1;
    }

    if ( tcgetattr(linkLayer.serialFileDescriptor,&(linkLayer.oldtio)) < 0 ) { /* save current port settings */
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
    newtio.c_cc[VTIME] = 1;   /* inter-character timer unused */
    newtio.c_cc[VMIN] = 0;   /* blocking read until 0 chars received */

  /*
    VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
    leitura do(s) próximo(s) caracter(es)
  */
    tcflush(linkLayer.serialFileDescriptor, TCIOFLUSH);

    if ( tcsetattr(linkLayer.serialFileDescriptor,TCSANOW,&newtio) == -1) {
        perror("Failed to set new settings for port, tcsetattr");
        close(linkLayer.serialFileDescriptor);
        return -1;
    }

    printf("New termios structure set\n");

    signal(SIGALRM, alarm_handler);  // Sets function alarm_handler as the handler of alarm signals

    int received = 0, res;
    uint8_t C;
    size_t cmdSize;
    uint8_t * cmd;

    if( linkLayer.is_receiver )
    	cmd = buildFrameHeader(A_CSENDER_RRECEIVER, C_UA, &cmdSize, false);
    else
    	cmd = buildFrameHeader(A_CSENDER_RRECEIVER, C_SET, &cmdSize, false);

    while(tries < linkLayer.settings->numAttempts) {
    		alarmed = false;
           	if ( linkLayer.is_receiver ) {
    			alarm(3);
    			C = readCMD();
    			if(C == C_SET) {
    				received = 1;
    				res = write(linkLayer.serialFileDescriptor, cmd, cmdSize); // o que fazer com res?
    			}
          	}
    		else {
    			res = write(linkLayer.serialFileDescriptor, cmd, cmdSize); // o que fazer com res?
    		    alarm(3);
    		    C = readCMD();
    		    if(C == C_UA)
    		    	received = 1;
    		}

           	if(received)
            	return 0;

    		tries++;
    }

    //Failed
    if ( tcsetattr(linkLayer.serialFileDescriptor,TCSANOW,&(linkLayer.oldtio)) < 0) { /* Restores old port settings */
    	perror("tcsetattr");
    	exit(1);
    }

    close(linkLayer.serialFileDescriptor);
    return -1;
}

int llwrite(uint8_t *packet, size_t packetSize) {
	/*
	size_t stuffedFrameSize;
	uint8_t * stuffedFrame = buildIFrame(packet, packetSize, &stuffedFrameSize);

	unsigned tries = 0;
	int received = 0, res;
    while(tries < linkLayer.settings->numAttempts) {
    		alarmed = false;

    		printf("Sending frame\n");
    		res = write(linkLayer.serialFileDescriptor, stuffedFrame, stuffedFrameSize); // o que fazer com res?
    		alarm(3);

    		//Máquina de estados


    		tries++;
    }

	*/

    return 0;
}

// errno != 0 em caso de erro
// retorna NULL e errno = 0, se receber disconnect e depois um UA para a applayer depois fazer llclose
// retorna endereço do pacote, *packetSize tamanho do pacote recebido
uint8_t* llread(size_t *payloadSize) {

    // Usar a variável receiverState para o estado ficar guardado entre chamadas, pq é global
    // Fazer unstuffing e cacular BCC
    // Se receber uma trama de informação válida, preenche o tamanho *packetSize, retorna o endereço do pacote (tem de estar unstuffed!)

    return NULL;
}

int llclose(void) {
    unsigned int tries = 0;

    if ( !blocked ) {
        fprintf(stderr, "You have to llinitialize and llopen first\n");
        return -1;
    }

    size_t DISCsize, UAsize;
    uint8_t * DISC = buildFrameHeader(A_CSENDER_RRECEIVER, C_DISC, &DISCsize, false);
    uint8_t * UA =  buildFrameHeader(A_CSENDER_RRECEIVER, C_UA, &UAsize, false);
    uint8_t C;
    int received = 0, res;

    while(tries < linkLayer.settings->numAttempts) {
    	alarmed = false;
        if ( linkLayer.is_receiver ) {
    		res = write(linkLayer.serialFileDescriptor, DISC, DISCsize);
    		alarm(3);
    		C = readCMD();
    		if(C == C_UA)
    			received = 1;
        }
    	else {
    		res = write(linkLayer.serialFileDescriptor, DISC, DISCsize);
    		alarm(3);
    		C = readCMD();
    		if(C == C_DISC)
    			received = 1;
    		if(received)
    			res = write(linkLayer.serialFileDescriptor, UA, UAsize); //o qie fazer com res
    		}

           	if(received)
            	break;
    		tries++;
    }

    if ( tcsetattr(linkLayer.serialFileDescriptor,TCSANOW,&(linkLayer.oldtio)) < 0 ) {
      perror("tcsetattr");
      close(linkLayer.serialFileDescriptor);
      return -1;
    }

    close(linkLayer.serialFileDescriptor);
    blocked = false;

    if(received)
    		return 0;
    	else return -1;
}




/**
 * More Functions
 */


static void alarm_handler(int signo) {
    alarmed = true;
    printf("alarm\n");
}

static int changeSequenceNumber() {
	return 1 - linkLayer.sequenceNumber;
}

static void print_frame(uint8_t * frame, size_t size) {
    size_t i;
    for(i = 0; i < size; i++)
        printf("%X ", frame[i]);
    printf("\n");
}

static bool isKnownC(uint8_t ch) {
	return (ch == C_SET || ch == C_UA || ch == C_DISC ||
			(ch & 0x7F) ==  C_RR_RAW || 	// Ao fazer AND se ch for RR com sequencia 1 ou 0, a expressao é verdadeira
			(ch & 0x7F)==  C_REJ_RAW);
}

static uint8_t readCMD() {
	uint8_t ch, C, BCC;
	bool stuffing = false;
	int state = 0, res;

	while(!alarmed) {
		res = read(linkLayer.serialFileDescriptor, &ch, 1);
		printf("res: %d\n", res);
		printf("State: %d", state);
		switch(state) {
			case 0:
				if(ch == F)
					state = 1;
				break;
			case 1:
				if(ch == A_CSENDER_RRECEIVER) {
					state = 2;
					BCC = ch;
				}
				else if(ch != F)
					state = 0;
				break;
			case 2:
				if(ch == F)
					state = 1;
				else {
					if(isKnownC(ch)) {
						BCC ^= ch;
						C = ch;
						state = 3;
					}
					else
						state = 0;
				}
				break;
			case 3:
				//Destuffing in run-time
				if(stuffing) {
					stuffing = false;
					if( (ch ^ STUFFING_XOR_BYTE) == BCC)
						state = 4;
					else if (ch == F)
						state = 1;
					else
						state = 0;
				}
				else {
					if(ch == ESC)
						stuffing = true;

					else if(ch == BCC)
						state = 4;
					else if(state == F)
						state = 1;
					else
						state = 0;
				}
				break;
			case 4:
				if(ch == F)
					return C; //Leu bem, devolve o C do comando
				else
					state = 0;
				break;
		}
	}

	return 0xFE; //Falhou, devolve -1
}

static uint8_t * stuff(uint8_t * packet, size_t size, size_t * stuffedSize) {
    *stuffedSize = size;

    size_t i;
    for(i = 0; i < size; i++)
        if(packet[i] == F || packet[i] == ESC)
            (*stuffedSize)++;

    uint8_t * stuffed = (uint8_t *) malloc (*stuffedSize);

    size_t j = 0;
    for(i = 0; i < size; i++) {
        if(packet[i] == ESC || packet[i] == F) {
            stuffed[j] = ESC;
            j++;
            stuffed[j] = STUFFING_XOR_BYTE ^ packet[i];
        }
        else
            stuffed[j] = packet[i];
        j++;
    }

    return stuffed;
}

static uint8_t* buildFrameHeader(uint8_t A, uint8_t C, size_t *headerSize, bool is_IframeHead) {
    size_t size = 4;
    bool stuffed = false;

    uint8_t BCC = A^C;
    if(BCC == F || BCC == ESC) {
        size++;
        stuffed = true;
        BCC ^= STUFFING_XOR_BYTE;
    }

    if(!is_IframeHead)
        size++;

    uint8_t *header = (uint8_t *) malloc (size);

    uint8_t i = 0;
    header[i++] = F;
    header[i++] = A;
    header[i++] = C;

    if(stuffed)
        header[i++] = ESC;

    header[i++] = BCC;

    if(!is_IframeHead)
        header[i] = F;

    *headerSize = size;
    return header;
}

static uint8_t * buildIFrame(uint8_t * packet, size_t packetSize, size_t * stuffedFrameSize) {
    size_t stuffedHeaderSize;
    uint8_t * stuffedHeader = buildFrameHeader(A_CSENDER_RRECEIVER, (linkLayer.sequenceNumber << 6) , &stuffedHeaderSize, true);

    size_t stuffedPacketSize;
    uint8_t * stuffedPacket = stuff(packet, packetSize, &stuffedPacketSize);

    *stuffedFrameSize = stuffedHeaderSize + stuffedPacketSize + 1; //Header + Packet + F
    uint8_t * stuffedFrame = (uint8_t *) malloc(*stuffedFrameSize);

    memcpy (stuffedFrame, stuffedHeader, stuffedHeaderSize);
    memcpy (stuffedFrame + stuffedHeaderSize, stuffedPacket, stuffedPacketSize);
    stuffedFrame[*stuffedFrameSize-1] = F;

    return stuffedFrame;
}

static uint8_t generateBcc(const uint8_t * data, size_t size) {
    size_t i;
    uint8_t bcc = 0x00;

    if ( data == NULL ) {
        errno = EINVAL;
        return 0;
    }

    for( i = 0; i < size; ++i)
        bcc ^= data[i];

    return bcc;
}
