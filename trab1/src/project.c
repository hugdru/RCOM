#include "parser.h"
#include "applayer.h"

// TESTING INCLUDES
#include "linklayer.h"

#define IS_RECEIVER(n) (!((n)>>4))
#define IS_TRANSMITTER(n) ((n)>>4)

void wipeBundles(void);

Bundle **Bundles= NULL;
size_t NBundles = 0;

int main(int argc, char *argv[])
{
    int i = 0;

    Bundles = parse_args(argc, argv, &NBundles);
    if ( Bundles == NULL) {
        print_usage(argv);
        goto cleanUp;
    }

    printf("NBundles: %lu\n", NBundles);
    for(i = 0; i < (int)NBundles; ++i) {
        if ( Bundles[i]->name != NULL)
            printf("Name: %s\n", Bundles[i]->name);

        printf("baudRate: %d\n", Bundles[i]->llSettings.baudRate);

        if ( Bundles[i]->llSettings.port != NULL )
            printf("port: %s\n", Bundles[i]->llSettings.port);

        printf("timeout: %d\n", Bundles[i]->llSettings.timeout);
        printf("numAttempts: %d\n", Bundles[i]->llSettings.numAttempts);
        printf("status: %d\n", Bundles[i]->alSettings.status);
        printf("packetBodySize: %lu\n", Bundles[i]->alSettings.packetBodySize);
    }

    llinitialize(&Bundles[0]->llSettings,IS_RECEIVER(Bundles[0]->alSettings.status));
    if ( llopen() != 0 ) goto cleanUp;
    llclose();

    /*if( initAppLayer(Bundles[0]) < 0)*/
        /*printf("Error: Initializing app layer\n");*/
    return 0;

cleanUp:
    wipeBundles();
    fprintf(stderr, "There was an error, report it to devs\n");
    return -1;
}

void wipeBundles(void) {
    size_t i;

    if ( Bundles == NULL || NBundles == 0 ) return;
    for( i = 0; i < NBundles; ++i) {
        free(Bundles[i]);
    }
}

