#include <termios.h>

typedef struct {
    char const *port;
    unsigned int timeout;
    unsigned int numAttempts;
    unsigned int IframeSize;
    tcflag_t baudRate;
} LinkLayerSettings;

