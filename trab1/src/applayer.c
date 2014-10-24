#include "applayeriomodes.h"
#include "applayer.h"
#include "useful.h"

#define IS_RECEIVER(n) (!((n)>>4))
#define IS_TRANSMITTER(n) ((n)>>4)

typedef struct AppLayer {
    int fileDescriptor;
    parsedAppLayerSettings *settings;
} AppLayer;

