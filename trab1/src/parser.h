#include <termios.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct parsedLinkLayerSettings {
    char const *port;
    unsigned int timeout;
    unsigned int numAttempts;
    tcflag_t baudRate;
} parsedLinkLayerSettings;

typedef struct parsedAppLayerSettings {
    int status;
    union Io {
        char *chptr;
        FILE *fptr;
    } io;
} parsedAppLayerSettings;

typedef struct Bundle {
    char const *name;
    parsedLinkLayerSettings pLlSettings;
    parsedAppLayerSettings pAlSettings;
} Bundle;

void print_usage(char **argv);
int parse_args(int argc, char **argv, size_t *NBundles, Bundle **Bundles);

