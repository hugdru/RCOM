#ifndef LINK_LAYER_H
#define LINK_LAYER_H

#include "useful.h"
#include "linklayersettings.h"
#include <stdint.h>
#include <stddef.h>

int llinitialize(LinkLayerSettings *ptr, bool is_receiver);
int llopen(void);
int llwrite(uint8_t *packet, size_t packetSize);
uint8_t* llread(uint16_t *payloadSize);
int llclose(void);

#endif

