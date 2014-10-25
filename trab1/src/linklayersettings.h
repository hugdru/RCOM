#ifndef LINK_LAYER_SETTINGS_H
#define LINK_LAYER_SETTINGS_H

#include <termios.h>

typedef struct {
    char const *port;
    unsigned int timeout;
    unsigned int numAttempts;
    unsigned int payloadSize;
    tcflag_t baudRate;
} LinkLayerSettings;

#endif
