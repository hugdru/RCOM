#ifndef LINK_LAYER_H
#define LINK_LAYER_H

#include "useful.h"
#include "linklayersettings.h"
#include <stdint.h>
#include <stddef.h>

int llinitialize(LinkLayerSettings *ptr, bool is_receiver);
int llopen(void);
int llwrite(uint8_t *packet, size_t packetSize);
int llread(uint8_t *packet);
int llclose(void);

#endif

