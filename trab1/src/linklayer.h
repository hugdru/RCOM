#ifndef LINK_LAYER_H
#define LINK_LAYER_H

#include "useful.h"
#include "linklayersettings.h"
#include <stdint.h>
#include <stddef.h>

int llinitialize(LinkLayerSettings *ptr, bool is_receiver);
int llopen(void);
int llwrite(char *packet, size_t packetSize);
char* llread(uint16_t *packetSize, int *error);
int llclose(void);

#endif

