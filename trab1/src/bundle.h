#include "applayersettings.h"
#include "linklayersettings.h"

typedef struct Bundle {
    char const *name;
    LinkLayerSettings llSettings;
    AppLayerSettings alSettings;
} Bundle;

