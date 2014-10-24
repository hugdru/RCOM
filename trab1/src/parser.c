#include "parser.h"
#include "useful.h"

#include <ctype.h>
#include <regex.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>

#define DEFAULT_BAUDRATE 38400
#define DEFAULT_MODEMDEVICE "/dev/ttyS0"
#define DEFAULT_TIMEOUT 3
#define DEFAULT_NUMATTEMPTS 3
#define DEFAULT_IFRAME_SIZE 100

unsigned long parse_ulong(char const * const str, int base); // From the function manual

void print_usage(char **argv) {

    char const *ptr;
    ptr = strrchr(argv[0],'/');
    if ( ptr == NULL ) ptr = argv[1];
    else ++ptr;

    printf("\nSends or Receives data from a given serial port");
    printf("\nUsage: %s [NTUNNELS] ([OPTIONS] MODE)*\n", ptr);

    printf("\nNTUNNELS:");
    printf("\n -N number\tNumber of Bundles to create, defaults to 1\n");
    printf("\n +\t\tTunnel (OPTIONS MODE) seperator\n");

    printf("\nOptions:\n");
    printf(" -b  Number\tChange baudRate to a certain value, defaults to 38400\n");
    printf(" -d  Path\tSet the serial port device file, defaults to /dev/ttyS0\n");
    printf(" -t  Number\tSeconds to timeout, defaults to 3 seconds\n");
    printf(" -r  Number\tNumber of retries before aborting connection, defaults to 3\n");
    printf(" -n  String\tName you wish to assign to this connection\n");
    printf(" -f  Number\tTamanho máximo do campo de informação das tramas I (sem stuffing)\n");

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
}

int parse_args(int argc, char **argv, size_t *NBundles, Bundle **Bundles) {

    int c, retn;
    unsigned int checkNDuplication = 0;
    bool ioSet;
    long int subArgc;
    char **subArgv, **oldSubArgv;
    size_t numberOfSeparators = 0;
    size_t i;
    unsigned long parsedNumber;
    regex_t deviceRegex;

    *NBundles = 1;

    // Find number of Bundles
    for ( i = 1; i < (size_t)argc; ++i) {
        if ( strncmp(argv[i],"-N",2) == 0 ) {
            if ( ( (strlen(argv[i]) > 2) && ((parsedNumber = parse_ulong(argv[i]+2,10)) != ULONG_MAX) ) ||
                 ( ((i+1) < (size_t)argc) && ((parsedNumber = parse_ulong(argv[i+1],10)) != ULONG_MAX) ) ) {
                *NBundles = parsedNumber;
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
    if ( (*NBundles == 1) && (numberOfSeparators != 0) ) *NBundles = numberOfSeparators + 1;

    // Create the array of pointers, each one pointing to a Bundle structure
    Bundles = (Bundle **) malloc(sizeof(Bundle *) * (*NBundles));
    for( i = 0; i < *NBundles; ++i) {
        Bundles[i] = (Bundle *) malloc(sizeof(Bundle));
        // Set defaults to all Bundles
        Bundles[i]->pLlSettings.baudRate = DEFAULT_BAUDRATE;
        if ( *NBundles == 1 ) Bundles[i]->pLlSettings.port = DEFAULT_MODEMDEVICE;
        Bundles[i]->pLlSettings.timeout = DEFAULT_TIMEOUT;
        Bundles[i]->pLlSettings.numAttempts = DEFAULT_NUMATTEMPTS;
        Bundles[i]->pLlSettings.IframeSize = DEFAULT_IFRAME_SIZE;
        Bundles[i]->pAlSettings.status = STATUS_UNSET;
        Bundles[i]->pAlSettings.io.fptr= NULL;
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
    for ( i = 0; i < *NBundles; ++i) {
        ioSet = false;
        if ( *NBundles != 1 ) {
            subArgc = 0;
            oldSubArgv = subArgv;
            while( (subArgv < (argv + argc)) && ((strncmp(*subArgv,"+",1) != 0) || subArgc == 0) ) {
                ++subArgv;
                ++subArgc;
            }
        }

        if ( (i == 0) && (subArgc == argc) && (*NBundles != 1) ) {
            errno = EINVAL;
            return -1;
        }

        while ( (c = getopt((int)subArgc, oldSubArgv,"N:b:d:t:r:n:S:R:m:f:x")) != -1 ) {

            if ( c == 'b' || c == 't' || c == 'r' || c == 'f' ) {
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
                    Bundles[i]->pLlSettings.baudRate = (tcflag_t)parsedNumber;
                    break;
                case 'd':
                    /* Regex testing */
                    retn = regexec(&deviceRegex, optarg, 0, NULL, 0);
                    if ( retn == REG_NOMATCH || retn != 0 ) {
                        fprintf(stderr, "Not a proper device file, expected /dev/ttyS[0-9]+");
                        errno = EINVAL;
                        return -1;
                    }
                    Bundles[i]->pLlSettings.port = optarg;
                    break;
                case 't':
                    Bundles[i]->pLlSettings.timeout = (unsigned int)parsedNumber;
                    break;
                case 'r':
                    Bundles[i]->pLlSettings.numAttempts = (unsigned int)parsedNumber;
                    break;
                case 'n':
                    Bundles[i]->name = optarg;
                    break;
                case 'S':
                    if ( (Bundles[i]->pAlSettings.io.fptr = fopen(optarg,"rb")) == NULL ) {
                        fprintf(stderr, "Error opening the file for reading\n");
                        return -1;
                    }
                    Bundles[i]->pAlSettings.status = STATUS_TRANSMITTER_FILE;
                    break;
                case 'R':
                    if ( (Bundles[i]->pAlSettings.io.fptr = fopen(optarg,"w+b")) == NULL ) {
                        fprintf(stderr, "Error opening the file for writing\n");
                        return -1;
                    }
                    Bundles[i]->pAlSettings.status = STATUS_RECEIVER_FILE;
                    break;
                case 'm':
                    Bundles[i]->pAlSettings.io.chptr = optarg;
                    Bundles[i]->pAlSettings.status = STATUS_TRANSMITTER_STRING;
                    break;
                case 'x':
                    Bundles[i]->pAlSettings.status = STATUS_TRANSMITTER_STREAM;
                    break;
                case 'f':
                    Bundles[i]->pLlSettings.IframeSize = (unsigned int)parsedNumber;
                    break;
                default:
                    errno = EINVAL;
                    return -1;
                    break;
            }
        }
        optind = 1;
        if ( !ioSet ) Bundles[i]->pAlSettings.status = STATUS_RECEIVER_STREAM;
    }

    return 0;
}

unsigned long parse_ulong(char const * const str, int base) {
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

