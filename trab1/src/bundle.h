#ifndef BUNDLE_H
#define BUNDLE_H

#include "linklayersettings.h"
#include "applayersettings.h"

typedef struct Bundle {
    char const * name;
    LinkLayerSettings llSettings;
    AppLayerSettings alSettings;
} Bundle;

#endif

