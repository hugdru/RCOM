/* Non-Canonical Processing*/
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <regex.h>
#include <limits.h>

#define _POSIX_SOURCE 1 /* POSIX compliant source */

// Trama UA
#define F 0x7e
#define A 0x03
#define C 0x03
#define B A^C

#define DEFAULT_BAUDRATE B38400
#define DEFAULT_MODEMDEVICE "/dev/ttyS0"
#define DEFAULT_TIMEOUT 3
#define DEFAULT_NUMTRANSMISSIONS 3

#define CONTAINER_SIZE 256
#define SIZE_OF_REPLY 5

#define STATUS_RECEIVER 0
#define STATUS_TRANSMITTER_FILE 1
#define STATUS_TRANSMITTER_STRING 2

typedef struct AppLayer {
    int fileDescriptor;
    int status;
} AppLayer;


typedef struct LinkLayer {
    tcflag_t baudRate;
    unsigned int sequenceNumber;
    char const *port;
    char frame[CONTAINER_SIZE];
    unsigned int timeout;
    unsigned int numTransmissions;
    struct termios oldtio;
} LinkLayer;

typedef struct Tunnel {
    char const *name;
    LinkLayer LLayer;
    AppLayer ALayer;
    union Io {
        char *chptr;
        FILE *fptr;
    } io;
} Tunnel;

static void print_usage( char const * const * const argv);
static int parse_args(const int argc, char const * const * const argv);
int llopen(Tunnel *ptr);
int llwrite(Tunnel *ptr, uint8_t *buffer, size_t length);
int llread(Tunnel *ptr, uint8_t *buffer);
int llclose(Tunnel *ptr);

static void alarm_handler(int signo);
static unsigned long parse_ulong(char const * const str, int base); // Aproveitado do exemplo do manual

typedef enum { false, true } bool;
static bool alarmed = false;
static uint8_t byteContainer[CONTAINER_SIZE];
static Tunnel **Tunnels;
static unsigned long NTunnels;

int main(const int argc, char const * const * const argv)
{
    uint8_t SET[5] = { F, A, C, B, F }; // Trama de Supervisão e Não Numeradas

    // PARSE STUFF FROM COMMAND LINE AND FILL THE STRUCTS
    // AND TEST THEIR VALIDITY, WITH BOUNDARY CHECKS AND REGEX
    if ( argc == 1 ) print_usage(argv);
    else parse_args(argc, argv);

    /*llopen();*/

    /*llwrite(fd,SET,sizeof(SET));*/

    /*llclose();*/

    return 0;
}

void print_usage( char const * const * const argv) {

    char const *ptr;
    ptr = strrchr(argv[0],'/');
    if ( ptr == NULL ) ptr = argv[1];
    else ++ptr;

    printf("\nSends or Receives data from a given serial port");
    printf("\nUsage: %s [NTUNNELS] ([OPTIONS] MODE)*\n", ptr);

    printf("\nNTUNNELS:");
    printf("\n -N number\tNumber of Tunnels to create, defaults to 1\n");
    printf("\n :\tTunnel (OPTIONS MODE) seperator\n");

    printf("\nOptions:\n");
    printf(" -b  Number\tChange baudRate to a certain value, defaults to 38400\n");
    printf(" -d  Path\tSet the serial port device file, defaults to /dev/ttyS0\n");
    printf(" -t  Number\tSeconds to timeout, defaults to 3 seconds\n");
    printf(" -r  Number\tNumber of retries before aborting connection, defaults to 3\n");
    printf(" -n  String\tName you wish to assign to this connection\n");

    printf("\nMODE");
    printf("\n Sender:\n");
    printf("     - \tInformation to send is read from stdin be it a pipe or redirection\n");
    printf("     < PathToFile\tSends a file must be used along with option -\n");
    printf("     -m Message\tSends a message\n");
    printf("\n Receiver (default (no args))\n");
    printf("     > PathToFile\tReceive information and place it in a file\n");
    printf("     -S  Path\tFile to send\n");
    printf("     -R  Path\tWhere to place received file\n");

    printf("\n--- Examples ---\n");
    printf("%s -N 2 -d '/dev/ttyS1' -t 5 -r 4 -S \"~/pictures/cat.jpeg\" : -d '/dev/ttyS2' -R \"~/passwords.kbd\"\n", ptr);
    printf("%s -d '/dev/ttyS1' - < 'dog.png'\n", ptr);
    printf("%s - < nuclearlaunchCodes\n", ptr);
    printf("cat file | %s -N 2 -d '/dev/ttyS1' -d '/dev/ttyS2' -\n", ptr);

}

int parse_args(const int argc, char const * const * const argv) {

    int c;
    int subArgc;
    char const * const * subArgv;
    size_t numberOfSeparators = 0;
    size_t i, t;
    unsigned long parsedNumber;

    NTunnels = 1;

    // Find number of Tunnels
    for ( i = 1; i < (size_t)argc; ++i) {
        if ( strncmp(argv[i],"-N",2) == 0 ) {
            // See if the number of Tunnels is adjoined to -N ex: -N"$1" ; -N1337
            if ( (strlen(argv[i]) > 2) && ((parsedNumber = parse_ulong(argv[i]+2,10)) == ULONG_MAX) ) return -1;
            // Check to see if the next arg is the number of NTunnels
            else if ( ((i+1) >= (size_t)argc) || ((parsedNumber = parse_ulong(argv[i+1],10)) == ULONG_MAX) ) return -1;
            NTunnels = parsedNumber;
            break;
        }
        else if ( strncmp(argv[i],":",1) == 0) {
            ++numberOfSeparators;
        }
    }

    // If user forgot -N option use the numberOfSeparators instead
    if ( (NTunnels == 1) && (numberOfSeparators != 0) ) NTunnels = numberOfSeparators + 1;

    // Create the array of pointers, each one pointing to a Tunnel structure
    Tunnels = (Tunnel **) malloc(sizeof(Tunnel *) * NTunnels);
    for( i = 0; i < NTunnels; ++i) {
        Tunnels[i] = (Tunnel *) malloc(sizeof(Tunnel));
        // Set defaults to all Tunnels
        Tunnels[i]->LLayer.baudRate = DEFAULT_BAUDRATE;
        if ( NTunnels == 1 ) Tunnels[i]->LLayer.port = DEFAULT_MODEMDEVICE;
        Tunnels[i]->LLayer.timeout = DEFAULT_TIMEOUT;
        Tunnels[i]->LLayer.numTransmissions = DEFAULT_NUMTRANSMISSIONS;
        Tunnels[i]->io.fptr = NULL;
    }

    // Parse stuff for each Tunnel
    for ( i = 0, subArgv = argv, subArgc = argc; i < NTunnels; ++i) {
        if ( NTunnels != 1 ) {
            subArgc = 0;
            while( (subArgv < argv) && (strncmp(*subArgv,":",1) != 0) ) {
                ++subArgv;
            }
        }
        while ( (c = getopt(subArgc, subArgv, "N:b:d:t:r:n:S:R:-m:")) != -1 ) {
            switch (c) {
                case 'b':
                    Tunnels[i]->LLayer.baudRate =;
                    break;
                case 'd':
                    Tunnels[i]->LLayer.port =;
                    break;
                case 't':
                    Tunnels[i]->LLayer.timeout =;
                    break;
                case 'r':
                    Tunnels[i]->LLayer.numTransmissions =;
                    break;
                case 'n':
                    Tunnels[i]->name=;
                case 'S':
                    Tunnels[i]->io.fptr=;
                    Tunnels[i]->ALayer.status=;
                    break;
                case 'R':
                    Tunnels[i]->io.fptr=;
                    Tunnels[i]->ALayer.status=;
                    break;
                case '-':
                    Tunnels[i]->io.fptr=;
                    Tunnels[i]->ALayer.status=;
                    break;
                case 'm':
                    Tunnels[i]->io.chptr=;
                    Tunnels[i]->ALayer.status=;
                    break;
                case '?':
                    if (optopt == 'c')
                    fprintf (stderr, "Option -%c requires an argument.\n", optopt);
                    else if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                    else
                    fprintf (stderr,
                            "Unknown option character `\\x%x'.\n",
                            optopt);
                    return 1;
                default:
                    errno = EINVAL;
                    return -1;
            }
        }
    }

    return 0;
}

int llopen(Tunnel *ptr) {

    struct termios newtio;

  /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
  */

    ptr->ALayer.fileDescriptor = open(ptr->LLayer.port, O_RDWR | O_NOCTTY);
    if ( ptr->ALayer.fileDescriptor < 0 ) {
        perror(ptr->LLayer.port);
        exit(-1);
    }

    if ( tcgetattr(ptr->ALayer.fileDescriptor,&(ptr->LLayer.oldtio)) == -1 ) { /* save current port settings */
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = ptr->LLayer.baudRate | CS8 | CLOCAL | CREAD;
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

    tcflush(ptr->ALayer.fileDescriptor, TCIOFLUSH);

    if ( tcsetattr(ptr->ALayer.fileDescriptor,TCSANOW,&newtio) == -1) {
      perror("Failed to set new settings for port, tcsetattr");
      exit(-1);
    }

    // New termios Structure set

    return 0;
}

int llwrite(Tunnel *ptr, uint8_t *buffer, size_t length) {

    ssize_t res;
    size_t numberOfTries = 0, readPackets = 0, i = 0;
    uint8_t Estado = 0, partOfReply;
    bool jump = false;

    if ( (buffer == NULL) || ( length == 0) ) {
        errno = EINVAL;
        return -1;
    }

    if (signal(SIGALRM, alarm_handler) == SIG_ERR) return -1;

    while(1) {
        if ( Estado == 0 ) {
            write(ptr->ALayer.fileDescriptor,buffer,length);
            alarm(ptr->LLayer.timeout);
            Estado = 1;
        } else {
            while(!alarmed) {
                    if ( readPackets == CONTAINER_SIZE ) {
                        errno = EBADMSG;
                        return -1;
                    } else if ( readPackets == SIZE_OF_REPLY ) { // Penso que as replies têm todas o mesmo tamanho
                        alarmed = true;
                        alarm(0); // Caso todos os pacotes já tenham chegado desativamos o alarm
                    }
                    res = read(ptr->ALayer.fileDescriptor,&partOfReply,1);
                    if ( res == 1 ) {
                        byteContainer[readPackets] = partOfReply;
                        ++readPackets;
                    }
            }
            // Podemos ter recebido o sinal sem termos recebido os pacotes todos
            // Dependendo do que queremos mandar temos de saber verificar as replies do receptor! São diferentes para diferentes situações
            // Tem de ser adicionado alguma coisa às structs para saber que reply tenho de testar
            /*if ( readPackets == CONTAINER_SIZE ) {*/
                /*for ( i = 0; i < length; ++i ) {*/
                    /*if ( buffer[i] != byteContainer[i] ) {*/
                        /*jump = true;*/
                        /*break;*/
                    /*}*/
                /*}*/
                /*if (!jump) return 0;*/
            /*}*/

            if ( numberOfTries >= ptr->LLayer.numTransmissions ) {
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


int llread(Tunnel *ptr, uint8_t *buffer) {
    return 0;
}

int llclose(Tunnel *ptr) {

    sleep(3);

    if ( tcsetattr(ptr->ALayer.fileDescriptor,TCSANOW,&(ptr->LLayer.oldtio)) == -1 ) {
      perror("tcsetattr");
      exit(-1);
    }

    close(ptr->ALayer.fileDescriptor);
    return 0;
}

void alarm_handler(int signo) {
    alarmed = true;
}

static unsigned long parse_ulong(char const * const str, int base) {
    char *endptr;
    unsigned long val;

    errno = 0;
    val = strtoul(str, &endptr, base);

    if ( ((errno == ERANGE) && (val == ULONG_MAX)) || (errno != 0 && val == 0)) {
        perror("strtol");
        return ULONG_MAX;
    }

    // If no digits were found
    if (endptr == str) {
        errno = EINVAL;
        return ULONG_MAX;
    }

    return val;
}
