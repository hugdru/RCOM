#include "useful.h"
#include "parser.h"

typedef struct AppLayer {
    int fileDescriptor;
    parsedAppLayerSettings *settings;
} AppLayer;

