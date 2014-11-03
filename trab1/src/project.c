#include "parser.h"
#include "applayer.h"

#include <stdlib.h>
#include <string.h>

#include "linklayer.h" // So para tests

#define IS_RECEIVER(n) (!((n)>>4)) // So para tests
#define IS_TRANSMITTER(n) ((n)>>4) // So para tests

void wipeBundles(void);

Bundle **Bundles= NULL;
size_t NBundles = 0;

int main(int argc, char *argv[])
{
    size_t i = 0;
    uint8_t stringy[20] = "OlaMacaco";

    Bundles = parse_args(argc, argv, &NBundles);
    if ( Bundles == NULL) {
        print_usage(argv);
        goto cleanUp;
    }

    // Testing
    fprintf(stderr, "NBundles: %lu\n", NBundles);
    for(i = 0; i < NBundles; ++i) {
        if ( Bundles[i]->name != NULL)
            fprintf(stderr, "Name: %s\n", Bundles[i]->name);

        fprintf(stderr, "baudRate: %d\n", Bundles[i]->llSettings.baudRate);

        if ( Bundles[i]->llSettings.port != NULL )
            fprintf(stderr, "port: %s\n", Bundles[i]->llSettings.port);

        fprintf(stderr, "timeout: %d\n", Bundles[i]->llSettings.timeout);
        fprintf(stderr, "numAttempts: %d\n", Bundles[i]->llSettings.numAttempts);
        fprintf(stderr, "status: %d\n", Bundles[i]->alSettings.status);
        fprintf(stderr, "packetBodySize: %lu\n", Bundles[i]->alSettings.packetBodySize);
        if ( Bundles[i]->alSettings.fileName != NULL )
            fprintf(stderr, "fileName: %s\n", Bundles[i]->alSettings.fileName);
    }

    llinitialize(&Bundles[0]->llSettings,IS_RECEIVER(Bundles[0]->alSettings.status)); // testing

    if ( llopen() < 0 ) {
        fprintf(stderr, "llopen() was unsuccessful\n");
        goto cleanUp; // testing
    }
    else fprintf(stderr, "llopen() was successful\n");

    size_t payloadSize;
    uint8_t *data;

  if( initAppLayer(Bundles[0]) < 0)
        fprintf(stderr, "Error: Initializing app layer\n");
    return 0;

cleanUp:
    wipeBundles();
    return -1;
}

void wipeBundles(void) {
    size_t i;

    if ( Bundles == NULL || NBundles == 0 ) return;
    for( i = 0; i < NBundles; ++i) {
        free(Bundles[i]);
    }
}

