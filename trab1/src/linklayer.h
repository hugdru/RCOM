#include "useful.h"
#include "parser.h"

#include <stdint.h>
#include <termios.h>

#define FRAME_SIZE 100

typedef struct LinkLayer {
    bool is_receiver;
    unsigned int sequenceNumber;
    char frame[FRAME_SIZE];
    struct termios oldtio;
    parsedLinkLayerSettings *settings;
    int serialFileDescriptor;
} LinkLayer;

int llinitialize(parsedLinkLayerSettings *ptr, bool is_receiver);
int llopen(void);
int lwrite(uint8_t *packet, size_t packetSize);
int llread(uint8_t *packet);
int llclose(void);

