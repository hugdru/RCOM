#ifndef APP_LAYER_SETTINGS_H
#define APP_LAYER_SETTINGS_H

#include <stdio.h>

#define STATUS_RECEIVER_FILE 0x00 // -R file, option
#define STATUS_RECEIVER_STREAM 0x01 // <, stdin
#define STATUS_TRANSMITTER_FILE 0x12 // -S file option
#define STATUS_TRANSMITTER_STRING 0x13 // -m 'foo'option
#define STATUS_TRANSMITTER_STREAM 0x14 // >, stdout
#define STATUS_UNSET -1

typedef struct {
    int status;
    size_t packetBodySize;
    char *fileName;

    union Io {
        char * chptr;
        FILE * fptr;
    } io;

} AppLayerSettings;

#endif

