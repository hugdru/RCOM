#include "useful.h"
#include "parser.h"

#include <stdint.h>
#include <termios.h>

int llinitialize(parsedLinkLayerSettings *ptr, bool is_receiver);
int llopen(void);
int lwrite(uint8_t *packet, size_t packetSize);
int llread(uint8_t *packet);
int llclose(void);

