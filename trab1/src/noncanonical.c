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
#include <getopt.h>

#define _POSIX_SOURCE 1 /* POSIX compliant source */

// Useful defines for SET & UA
#define F 0x7e
#define A 0x03
#define C 0x03
#define B ((A)^(C))

// All Tunnels get these values in case user does not overwrite them
#define DEFAULT_BAUDRATE 38400
#define DEFAULT_MODEMDEVICE "/dev/ttyS0"
#define DEFAULT_TIMEOUT 3
#define DEFAULT_NUMATTEMPTS 3

#define CONTAINER_SIZE 256
#define SIZE_OF_FRAMESU 5

#define IS_RECEIVER(n) (!((n)>>4))
#define IS_TRANSMITTER(n) ((n)>>4)
#define STATUS_RECEIVER_FILE 0x00
#define STATUS_RECEIVER_STREAM 0x01
#define STATUS_TRANSMITTER_FILE 0x12
#define STATUS_TRANSMITTER_STRING 0x13
#define STATUS_TRANSMITTER_STREAM 0x14
#define STATUS_UNSET -1

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
    unsigned int numAttempts;
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

static void print_usage(char **argv);
static int parse_args(int argc, char **argv);
int llopen(Tunnel *ptr);
int llwrite(Tunnel *ptr);
int llread(Tunnel *ptr);
int llclose(Tunnel *ptr);

static void alarm_handler(int signo);
static unsigned long parse_ulong(char const * const str, int base); // From the function manual

typedef enum { false, true } bool;
static bool alarmed = false;
static uint8_t byteContainer[CONTAINER_SIZE];
static Tunnel **Tunnels;
static unsigned long NTunnels;

// Multidimensional array for supervision and Not numbered frames
#define SET_OFFSET 0
#define UA_OFFSET 0
#define RR_OFFSET 1
#define DISC_OFFSET 2
static uint8_t framesSU[][SIZE_OF_FRAMESU] = {{F,A,C,A^C,F}, {0}, {0}};

int main(int argc, char *argv[])
{
    int i = 0;

    i = parse_args(argc, argv);
    if ( i == -1 ) print_usage(argv);

    printf("NTunnels: %lu\n", NTunnels);
    for(i = 0; i < (int)NTunnels; ++i) {
        printf("Name: %s\n", Tunnels[i]->name);
        printf("baudRate: %d\n", Tunnels[i]->LLayer.baudRate);
        printf("port: %s\n", Tunnels[i]->LLayer.port);
        printf("timeout: %d\n", Tunnels[i]->LLayer.timeout);
        printf("numAttempts: %d\n", Tunnels[i]->LLayer.numAttempts);
        printf("status: %d\n", Tunnels[i]->ALayer.status);
    }

    llopen(Tunnels[0]);

    /*llwrite(Tunnels[0]);*/

    llclose(Tunnels[0]);

    return 0;
}

void print_usage(char **argv) {

    char const *ptr;
    ptr = strrchr(argv[0],'/');
    if ( ptr == NULL ) ptr = argv[1];
    else ++ptr;

    printf("\nSends or Receives data from a given serial port");
    printf("\nUsage: %s [NTUNNELS] ([OPTIONS] MODE)*\n", ptr);

    printf("\nNTUNNELS:");
    printf("\n -N number\tNumber of Tunnels to create, defaults to 1\n");
    printf("\n +\t\tTunnel (OPTIONS MODE) seperator\n");

    printf("\nOptions:\n");
    printf(" -b  Number\tChange baudRate to a certain value, defaults to 38400\n");
    printf(" -d  Path\tSet the serial port device file, defaults to /dev/ttyS0\n");
    printf(" -t  Number\tSeconds to timeout, defaults to 3 seconds\n");
    printf(" -r  Number\tNumber of retries before aborting connection, defaults to 3\n");
    printf(" -n  String\tName you wish to assign to this connection\n");

    printf("\nMODE");
    printf("\n Sender:\n");
    printf("     -x \t\tInformation to send is read from stdin be it a pipe or redirection\n");
    printf("     < PathToFile\tSends a file must be used along with option -\n");
    printf("     -m Message\t\tSends a message\n");
    printf("\n Receiver (default (no args))\n");
    printf("     > PathToFile\tReceive information and place it in a file\n");
    printf("     -S  Path\t\tFile to send\n");
    printf("     -R  Path\t\tWhere to place received file\n");

    printf("\n--- Examples ---\n");
    printf("%s -N 2 -d '/dev/ttyS1' -t 5 -r 4 -S \"~/pictures/cat.jpeg\" + -d '/dev/ttyS2' -R \"~/passwords.kbd\"\n", ptr);
    printf("%s -d '/dev/ttyS1' -x < 'dog.png'\n", ptr);
    printf("%s -x < nuclearlaunchCodes\n", ptr);
    printf("cat file | %s -N 2 -d '/dev/ttyS1' -d '/dev/ttyS2' -x\n", ptr);

    exit(EXIT_FAILURE);
}

int parse_args(int argc, char **argv) {

    int c, retn;
    unsigned int checkNDuplication = 0;
    bool ioSet;
    long int subArgc;
    char **subArgv, **oldSubArgv;
    size_t numberOfSeparators = 0;
    size_t i;
    unsigned long parsedNumber;
    regex_t deviceRegex;

    NTunnels = 1;

    // Find number of Tunnels
    for ( i = 1; i < (size_t)argc; ++i) {
        if ( strncmp(argv[i],"-N",2) == 0 ) {
            if ( ( (strlen(argv[i]) > 2) && ((parsedNumber = parse_ulong(argv[i]+2,10)) != ULONG_MAX) ) ||
                 ( ((i+1) < (size_t)argc) && ((parsedNumber = parse_ulong(argv[i+1],10)) != ULONG_MAX) ) ) {
                NTunnels = parsedNumber;
                break;
            } else {
                fprintf(stderr, "The -N option must be followed by a number space separated or adjoined");
                return -1;
            }
        } else if ( strncmp(argv[i],"+",1) == 0) {
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
        Tunnels[i]->LLayer.numAttempts = DEFAULT_NUMATTEMPTS;
        Tunnels[i]->ALayer.status = STATUS_UNSET;
        Tunnels[i]->io.fptr = NULL;
    }

    retn = regcomp(&deviceRegex,"/dev/ttyS[0-9][0-9]*",0);
    if(retn) {
        fprintf(stderr, "Could not compile regex\n");
        return -1;
    }
    // Parse stuff for each Tunnel
    subArgv = argv;
    subArgc = argc;
    oldSubArgv = argv;
    for ( i = 0; i < NTunnels; ++i) {
        ioSet = false;
        if ( NTunnels != 1 ) {
            subArgc = 0;
            oldSubArgv = subArgv;
            while( (subArgv < (argv + argc)) && ((strncmp(*subArgv,"+",1) != 0) || subArgc == 0) ) {
                ++subArgv;
                ++subArgc;
            }
        }

        if ( (i == 0) && (subArgc == argc) && (NTunnels != 1) ) {
            errno = EINVAL;
            return -1;
        }

        while ( (c = getopt((int)subArgc, oldSubArgv,"N:b:d:t:r:n:S:R:m:x")) != -1 ) {

            if ( c == 'b' || c == 't' || c == 'r') {
                parsedNumber = parse_ulong(optarg,10);
                if ( parsedNumber == ULONG_MAX ) {
                    fprintf(stderr, "-%c must be followed by a number\n", c);
                    return -1;
                }
            } else if ( c == 'S' || c == 'R' || c == 'x' || c == 'm' ) {
                if ( ioSet ) {
                    fprintf(stderr, "There can only be a mode for each tunnel");
                    return -1;
                }
                ioSet = true;
            }

            switch (c) {
                case 'N':
                    if ( checkNDuplication > 1 ) {
                        fprintf(stderr, "There can only be a -N option");
                        errno = EINVAL;
                        return -1;
                    }
                    ++checkNDuplication;
                    break;
                case 'b':
                    Tunnels[i]->LLayer.baudRate = (tcflag_t)parsedNumber;
                    break;
                case 'd':
                    /* Regex testing */
                    retn = regexec(&deviceRegex, optarg, 0, NULL, 0);
                    if ( retn == REG_NOMATCH || retn != 0 ) {
                        fprintf(stderr, "Not a proper device file, expected /dev/ttyS[0-9]+");
                        errno = EINVAL;
                        return -1;
                    }
                    Tunnels[i]->LLayer.port = optarg;
                    break;
                case 't':
                    Tunnels[i]->LLayer.timeout = (unsigned int)parsedNumber;
                    break;
                case 'r':
                    Tunnels[i]->LLayer.numAttempts = (unsigned int)parsedNumber;
                    break;
                case 'n':
                    Tunnels[i]->name = optarg;
                    break;
                case 'S':
                    if ( (Tunnels[i]->io.fptr = fopen(optarg,"rb")) == NULL ) {
                        fprintf(stderr, "Error opening the file for reading\n");
                        return -1;
                    }
                    Tunnels[i]->ALayer.status = STATUS_TRANSMITTER_FILE;
                    break;
                case 'R':
                    if ( (Tunnels[i]->io.fptr = fopen(optarg,"w+b")) == NULL ) {
                        fprintf(stderr, "Error opening the file for writing\n");
                        return -1;
                    }
                    Tunnels[i]->ALayer.status = STATUS_RECEIVER_FILE;
                    break;
                case 'm':
                    Tunnels[i]->io.chptr = optarg;
                    Tunnels[i]->ALayer.status = STATUS_TRANSMITTER_STRING;
                    break;
                case 'x':
                    Tunnels[i]->ALayer.status = STATUS_TRANSMITTER_STREAM;
                    break;
                default:
                    errno = EINVAL;
                    return -1;
                    break;
            }
        }
        optind = 1;
        if ( !ioSet ) Tunnels[i]->ALayer.status = STATUS_RECEIVER_STREAM;
    }

    return 0;
}

int llopen(Tunnel *ptr) {

    bool first;
    unsigned int tries = 0;
    struct termios newtio;
    uint8_t partOfFrame, state;

  /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
  */

    ptr->ALayer.fileDescriptor = open(ptr->LLayer.port, O_RDWR | O_NOCTTY);
    if ( ptr->ALayer.fileDescriptor < 0 ) {
        perror(ptr->LLayer.port);
        exit(-1);
    }

    errno = 0;
    if ( tcgetattr(ptr->ALayer.fileDescriptor,&(ptr->LLayer.oldtio)) == -1 ) { /* save current port settings */
        printf("errno = %d\n", errno);
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
    signal(SIGALRM, alarm_handler);  // Sets function alarm_handler as the handler of alarm signals

    while(tries < ptr->LLayer.numAttempts) {

        if ( IS_TRANSMITTER(ptr->ALayer.status) ) {
            write(ptr->ALayer.fileDescriptor, framesSU[SET_OFFSET], 5);
        }

        alarmed = false;
        alarm(3);
        first = true;

        while(!alarmed) {
            read(ptr->ALayer.fileDescriptor, &partOfFrame, 1);

            switch (state) {
                case 0:
                    if (partOfFrame == F)
                        if ( IS_RECEIVER(ptr->ALayer.status) && first ) {
                            alarm(3);
                            alarmed = false;
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
                    if ( IS_RECEIVER(ptr->ALayer.status) ) {
                        write(ptr->ALayer.fileDescriptor,framesSU[UA_OFFSET],SIZE_OF_FRAMESU);
                    }
                    return 0; //Succeeded
                default:
                    return -1;
            }
        }
        tries++;
    }
    errno = ECONNABORTED;
    return -1;
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


int llread(Tunnel *ptr) {
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

