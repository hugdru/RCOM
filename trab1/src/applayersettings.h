#ifndef APP_LAYER_SETTINGS_H
#define APP_LAYER_SETTINGS_H

#include <stdio.h>

#define STATUS_RECEIVER_FILE 0x00 // -R file
#define STATUS_RECEIVER_FILE_RECEIVED_NAME 0x02 // -D
#define STATUS_RECEIVER_STREAM 0x01 // <, stdin
#define STATUS_TRANSMITTER_FILE 0x12 // -S file
#define STATUS_TRANSMITTER_STRING 0x13 // -m 'foo'
#define STATUS_TRANSMITTER_STREAM 0x14 // >
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

