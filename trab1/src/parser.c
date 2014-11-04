#include "useful.h"
#include "parser.h"

#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#define DEFAULT_BAUDRATE 38400
#define DEFAULT_MODEMDEVICE "/dev/ttyS1"
#define DEFAULT_TIMEOUT 3
#define DEFAULT_NUMATTEMPTS 3
#define DEFAULT_PAYLOAD_SIZE 100
#define DEFAULT_PACKETBODY_SIZE 50

static unsigned long parse_ulong(char const * const str, int base); // From the function manual

void print_usage(char **argv) {

    char const *ptr;
    ptr = strrchr(argv[0], '/');
    if (ptr == NULL)
        ptr = argv[1];
    else
        ++ptr;

    fprintf(stderr, "\nSends or Receives data from a given serial port");
    fprintf(stderr, "\nUsage: %s [NTUNNELS] ([OPTIONS] MODE)*\n", ptr);

    fprintf(stderr, "\nNTUNNELS:");
    fprintf(stderr,
            "\n -N number\tNumber of Bundles to create, defaults to 1\n");
    fprintf(stderr, "\n +\t\tTunnel (OPTIONS MODE) seperator\n");

    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr,
            " -h  \t\tFor help\n");
    fprintf(stderr,
            " -b  Number\tChange baudRate to a certain value, defaults to 38400\n");
    fprintf(stderr,
            " -d  Path\tSet the serial port device file, defaults to /dev/ttyS0\n");
    fprintf(stderr, " -t  Number\tSeconds to timeout, defaults to 3 seconds\n");
    fprintf(stderr,
            " -r  Number\tNumber of retries before aborting connection, defaults to 3\n");
    fprintf(stderr,
            " -n  String\tName you wish to assign to this connection\n");
    fprintf(stderr,
            " -f  Number\tTamanho máximo do payload das tramas I (sem stuffing)\n");
    fprintf(stderr,
            " -s  Number\tTamanho máximo da parte do pacote(body) que contém a informação útil\n");

    fprintf(stderr, "\nMODE");
    fprintf(stderr, "\n Sender:\n");
    fprintf(stderr,
            "     -x \t\tInformation to send is read from stdin be it a pipe or redirection\n");
    fprintf(stderr,
            "     < PathToFile\tSends a file must be used along with option -x\n");
    fprintf(stderr, "     -m Message\t\tSends a message\n");
    fprintf(stderr, "\n Receiver (default (no args))\n");
    fprintf(stderr, "     > PathToFile\tReceive information and place it in a file\n");
    fprintf(stderr, "     -S  Path\t\tFile to send\n");
    fprintf(stderr, "     -R  Path\t\tWhere to place received file\n");
    fprintf(stderr, "     -D  \t\tUse the fileName that comes in control packet start\n");

    fprintf(stderr, "\n--- Examples ---\n");
    fprintf(stderr, "%s -h\n", ptr);
    fprintf(stderr,
            "%s -N 2 -d '/dev/ttyS1' -t 5 -r 4 -S \"~/pictures/cat.jpeg\" + -d '/dev/ttyS2' -R \"~/passwords.kbd\"\n",
            ptr);
    fprintf(stderr, "%s -d '/dev/ttyS1' -x < 'dog.png'\n", ptr);
    fprintf(stderr, "%s -x < nuclearlaunchCodes\n", ptr);
    fprintf(stderr, "cat file | %s -N 2 -d '/dev/ttyS1' -d '/dev/ttyS2' -x\n", ptr);
}

Bundle** parse_args(int argc, char **argv, size_t *NBundles) {

    /*int c, retn;*/
    int c;
    unsigned int checkNDuplication = 0;
    bool ioSet;
    long int subArgc;
    char **subArgv, **oldSubArgv;
    size_t numberOfSeparators = 0;
    size_t i;
    unsigned long parsedNumber;
    char *ptr;
    /*regex_t deviceRegex;*/
    Bundle **Bundles;

    *NBundles = 1;

    // Find number of Bundles
    for (i = 1; i < (size_t) argc; ++i) {
        if (strncmp(argv[i], "-N", 2) == 0) {
            if (((strlen(argv[i]) > 2)
                    && ((parsedNumber = parse_ulong(argv[i] + 2, 10))
                            != ULONG_MAX))
                    || (((i + 1) < (size_t) argc) && ((parsedNumber =
                            parse_ulong(argv[i + 1], 10)) != ULONG_MAX))) {
                *NBundles = parsedNumber;
                break;
            } else {
                fprintf(stderr,
                        "The -N option must be followed by a number space separated or adjoined");
                return NULL;
            }
        } else if (strncmp(argv[i], "+", 1) == 0) {
            ++numberOfSeparators;
        }
    }

    // If user forgot -N option use the numberOfSeparators instead
    if ((*NBundles == 1) && (numberOfSeparators != 0))
        *NBundles = numberOfSeparators + 1;

    // Create the array of pointers, each one pointing to a Bundle structure
    Bundles = (Bundle **) malloc(sizeof(Bundle *) * (*NBundles));
    for (i = 0; i < *NBundles; ++i) {
        Bundles[i] = (Bundle *) malloc(sizeof(Bundle));
        // Set defaults to all Bundles
        Bundles[i]->llSettings.baudRate = DEFAULT_BAUDRATE;
        Bundles[i]->llSettings.port = DEFAULT_MODEMDEVICE;
        Bundles[i]->llSettings.timeout = DEFAULT_TIMEOUT;
        Bundles[i]->llSettings.numAttempts = DEFAULT_NUMATTEMPTS;
        Bundles[i]->llSettings.payloadSize = DEFAULT_PAYLOAD_SIZE;
        Bundles[i]->alSettings.status = STATUS_UNSET;
        Bundles[i]->alSettings.io.fptr = NULL;
        Bundles[i]->alSettings.packetBodySize = DEFAULT_PACKETBODY_SIZE;
        Bundles[i]->alSettings.fileName = NULL;
        Bundles[i]->name = NULL;
    }

    /*retn = regcomp(&deviceRegex,"/dev/ttyS[0-9][0-9]*",0);*/
    /*if(retn) {*/
    /*fprintf(stderr, "Could not compile regex\n");*/
    /*return NULL;*/
    /*}*/
    // Parse stuff for each Tunnel
    subArgv = argv;
    subArgc = argc;
    oldSubArgv = argv;
    for (i = 0; i < *NBundles; ++i) {
        ioSet = false;
        if (*NBundles != 1) {
            subArgc = 0;
            oldSubArgv = subArgv;
            while ((subArgv < (argv + argc))
                    && ((strncmp(*subArgv, "+", 1) != 0) || subArgc == 0)) {
                ++subArgv;
                ++subArgc;
            }
        }

        if ((i == 0) && (subArgc == argc) && (*NBundles != 1)) {
            errno = EINVAL;
            return NULL;
        }

        while ((c = getopt((int) subArgc, oldSubArgv, "N:b:d:t:r:n:S:R:m:f:s:xhD"))
                != -1) {

            if (c == 'b' || c == 't' || c == 'r' || c == 'f' || c == 's') {
                parsedNumber = parse_ulong(optarg, 10);
                if (parsedNumber == ULONG_MAX) {
                    fprintf(stderr, "-%c must be followed by a number\n", c);
                    return NULL;
                }
            } else if (c == 'S' || c == 'R' || c == 'x' || c == 'm' || c == 'D') {
                if (ioSet) {
                    fprintf(stderr, "There can only be a mode for each tunnel");
                    return NULL;
                }
                ioSet = true;
            }

            switch (c) {
            case 'N':
                if (checkNDuplication > 1) {
                    fprintf(stderr, "There can only be a -N option");
                    errno = EINVAL;
                    return NULL;
                }
                ++checkNDuplication;
                break;
            case 'b':
                Bundles[i]->llSettings.baudRate = (tcflag_t) parsedNumber;
                break;
            case 'd':
                /* Regex testing */
                /*retn = regexec(&deviceRegex, optarg, 0, NULL, 0);*/
                /*if ( retn == REG_NOMATCH || retn != 0 ) {*/
                /*fprintf(stderr, "Not a proper device file, expected /dev/ttyS[0-9]+");*/
                /*errno = EINVAL;*/
                /*return NULL;*/
                /*}*/
                Bundles[i]->llSettings.port = optarg;
                break;
            case 't':
                Bundles[i]->llSettings.timeout = (unsigned int) parsedNumber;
                break;
            case 'r':
                Bundles[i]->llSettings.numAttempts =
                        (unsigned int) parsedNumber;
                break;
            case 'n':
                Bundles[i]->name = optarg;
                break;
            case 'S':
                if ((Bundles[i]->alSettings.io.fptr = fopen(optarg, "rb"))
                        == NULL) {
                    fprintf(stderr, "Error opening the file for reading\n");
                    return NULL;
                }
                Bundles[i]->alSettings.status = STATUS_TRANSMITTER_FILE;
                ptr = strrchr(optarg, '/');
                if (ptr == NULL)
                    Bundles[i]->alSettings.fileName = optarg;
                else
                    Bundles[i]->alSettings.fileName = ++ptr;
                break;
            case 'R':
                if ((Bundles[i]->alSettings.io.fptr = fopen(optarg, "w+b"))
                        == NULL) {
                    fprintf(stderr, "Error opening the file for writing\n");
                    return NULL;
                }
                Bundles[i]->alSettings.status = STATUS_RECEIVER_FILE;
                ptr = strrchr(optarg, '/');
                if (ptr == NULL)
                    Bundles[i]->alSettings.fileName = optarg;
                else
                    Bundles[i]->alSettings.fileName = ++ptr;
                break;
            case 'm':
                Bundles[i]->alSettings.io.chptr = optarg;
                Bundles[i]->alSettings.status = STATUS_TRANSMITTER_STRING;
                break;
            case 'f':
                Bundles[i]->llSettings.payloadSize =
                        (unsigned int) parsedNumber;
                break;
            case 's':
                Bundles[i]->alSettings.packetBodySize =
                        (unsigned int) parsedNumber;
                break;
            case 'x':
                Bundles[i]->alSettings.status = STATUS_TRANSMITTER_STREAM;
                break;
            case 'D':
                Bundles[i]->alSettings.status = STATUS_RECEIVER_FILE_RECEIVED_NAME;
                break;
            default:
                errno = EINVAL;
                return NULL;
                break;
            }
        }
        optind = 1;
        if (!ioSet)
            Bundles[i]->alSettings.status = STATUS_RECEIVER_STREAM;
    }

    return Bundles;
}


static unsigned long parse_ulong(char const * const str, int base) {
    char *endptr;
    unsigned long val;

    errno = 0;
    val = strtoul(str, &endptr, base);

    if (((errno == ERANGE) && (val == ULONG_MAX)) || (errno != 0 && val == 0)) {
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

