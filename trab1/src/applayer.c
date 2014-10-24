#include "applayer.h"
#include "linklayer.h"

#define IS_RECEIVER(n) (!((n)>>4))
#define IS_TRANSMITTER(n) ((n)>>4)

typedef struct AppLayer {
    int fileDescriptor;
    AppLayerSettings *settings;
} AppLayer;

