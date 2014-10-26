#ifndef BUNDLE_H
#define BUNDLE_H

#include "applayersettings.h"
#include "linklayersettings.h"

typedef struct Bundle {
    char const *name;
    LinkLayerSettings llSettings;
    AppLayerSettings alSettings;
} Bundle;

#endif

