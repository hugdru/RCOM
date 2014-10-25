#include "parser.h"

void wipeBundles(void);

Bundle **Bundles= NULL;
size_t NBundles = 0;

int main(int argc, char *argv[])
{
    int i = 0;

    i = parse_args(argc, argv, &NBundles, Bundles);
    if ( i == -1 ) {
        print_usage(argv);
        goto cleanup;
    }

    printf("NBundles: %lu\n", NBundles);
    for(i = 0; i < (int)NBundles; ++i) {
        printf("Name: %s\n", Bundles[i]->name);
        printf("baudRate: %d\n", Bundles[i]->llSettings.baudRate);
        printf("port: %s\n", Bundles[i]->llSettings.port);
        printf("timeout: %d\n", Bundles[i]->llSettings.timeout);
        printf("numAttempts: %d\n", Bundles[i]->llSettings.numAttempts);
        printf("status: %d\n", Bundles[i]->alSettings.status);
    }


    wipeBundles();
    return 0;

cleanup:
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

