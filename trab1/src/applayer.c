#include "applayer.h"
#include "linklayer.h"

#include <string.h>
#include <stdlib.h>

#define IS_RECEIVER(n) (!((n)>>4))
#define IS_TRANSMITTER(n) ((n)>>4)

// Control byte types
#define C_DATA 0x01
#define C_START 0x02
#define C_END 0x03

// Start Packet Argument Types
#define TYPE_FILESIZE 0
#define TYPE_FILENAME 1

typedef struct {
    int sequenceNumber;
    long int fileSize;
    AppLayerSettings * settings;
} AppLayer;

AppLayer appLayer;

/**
 * @desc Faz parser do pacote recebido
 * @arg uint8_t* packet: pacote recebido
 * @arg size_t size: número de bytes de packet
 * @rrturn Retorna 1 quando a mensagem foi toda lida, 0 se não
 */
static int parserPacket(uint8_t* packet, size_t size);

/**
 * @des Reads message/file from sender
 */
static int read(void);

/**
 * @desc Envia pacote de controlo do tipo START contendo o nome do ficheiro e tamanho
 * @return Retorna um número positivo em caso de sucesso e negatio em caso de erro
 */
static int writeStartPacket(void);

/**
 * @desc Envia pacote de controlo do tipo END contendo apenas o byte de controlo correspondendo a type end
 * @return Retorna um número positivo em caso de sucesso e negatio em caso de erro
 */
static int writeEndPacket(void);

/**
 * @des Envia pacote de controlo do tipo DATA com data recebida como argumento
 * @arg uint8_t *data: data a ser empacotada e enviada
 * @arg size_t size: número de bytes de data
 * @return Retorna um número positivo em caso de sucesso e negatio em caso de erro
 */
static int writeDataPacket(uint8_t *data, size_t size);

/**
 * @desc Sends message/file to the receiver
 */
static int write(void);


int initAppLayer(Bundle *bundle) {

    int res;

    if ( bundle == NULL ) {
        fprintf(stderr, "Error: bundle is null\n");
        errno = EINVAL;
        return 1;
    }

    if ( bundle->alSettings.packetBodySize > (256 * 255 + 255) ) {
        fprintf(stderr, "packetBodySize exceeds the maximum supported value");
        errno = EINVAL;
        return -1;
    }

    appLayer.settings = &bundle->alSettings;
    appLayer.fileSize = 0;

    if ( appLayer.settings->status == STATUS_TRANSMITTER_FILE ) {
        if ( fseek(appLayer.settings->io.fptr, 0, SEEK_END) ){
            if ( appLayer.settings->fileName != NULL ) {
                fprintf(stderr, "Error: Cant's find file '%s' size", appLayer.settings->fileName);
            } else fprintf(stderr, "appLayer.settings->fileName is set to Null in TRANSMITTER_FILE mode");
            return -1;
        }
        appLayer.fileSize = ftell(appLayer.settings->io.fptr);
    } else if (appLayer.settings->status == STATUS_RECEIVER_FILE ) {
        if ( appLayer.settings->fileName == NULL ) {
            fprintf(stderr, "appLayer.settings->fileName is set to Null in RECEIVER_FILE mode");
            return -1;
        }
    } else if (appLayer.settings->status == STATUS_RECEIVER_FILE_RECEIVED_NAME ) {
        fprintf(stderr, "Gonna create fileName when control packet start arrives\n");
    } else if (appLayer.settings->status == STATUS_TRANSMITTER_STRING) {
        appLayer.fileSize = strlen(appLayer.settings->io.chptr) + 1;
    } else {
        fprintf(stderr, "Redirections and pipes are not implemented yet\n");
        return -1;
    }

    llinitialize(&(bundle->llSettings), IS_RECEIVER(appLayer.settings->status));

    if ( llopen() != 0 ) {
        fprintf(stderr, "Error: llopen()\n");
        return -1;
    }
    else fprintf(stderr, "llopen() was successful\n\n");

    appLayer.sequenceNumber = 0;

    if ( IS_RECEIVER(appLayer.settings->status) ) {
        res = read();
        if ( res != 0 ) {
            fprintf(stderr, "There was an error in applayer read function\n");
            return -1;
        }
    } else {
        res = write();
        if ( res != 0 ) {
            fprintf(stderr, "There was an error in applayer write function\n");
            return -1;
        }
    }

    if( llclose() != 0) {
        fprintf(stderr, "Error: llclose()\n");
        return -1;
    }

    if ( appLayer.settings->status == STATUS_TRANSMITTER_FILE || appLayer.settings->status == STATUS_RECEIVER_FILE ) fclose(appLayer.settings->io.fptr);

    else fprintf(stderr, "llclose() was successful\n");

    return 0;
}

static int parserPacket(uint8_t* packet, size_t size) {
    uint8_t C = packet[0];
    int res;

    if(C == C_DATA) {
        uint8_t sequence = packet[1];
        uint8_t L2 = packet[2];
        uint8_t L1 = packet[3];
        uint32_t dataSize = 256 * L2 + L1;

        if ( sequence != appLayer.sequenceNumber ) {
            fprintf(stderr, "parserPacket: numero de sequência inválido\n");
            errno = ECONNABORTED;
            return -1;
        }

        if ( (appLayer.settings->status == STATUS_RECEIVER_FILE_RECEIVED_NAME) && appLayer.settings->io.fptr == NULL ) {
            fprintf(stderr, "parserPacket: esperava um C_START antes do C_DATA\n");
            return -1;
        }

        if( appLayer.settings->status == STATUS_RECEIVER_FILE || appLayer.settings->status == STATUS_RECEIVER_FILE_RECEIVED_NAME) {
            res = fwrite(packet+4, 1, dataSize, appLayer.settings->io.fptr);
            if ( ferror(appLayer.settings->io.fptr) ) {
                fprintf(stderr, "parserPacket: erro ao escrever para o ficheiro\n");
                return -1;
            }
            fprintf(stderr, "parserPacket: number of bytes written to file %d\n", res);
            appLayer.fileSize += res;
            ++appLayer.sequenceNumber;
            if ( appLayer.sequenceNumber > 255 ) appLayer.sequenceNumber = 0;
        } else {
            // Pipes e redireccões não foram implementadas
        }
    } else if ( C == C_START ) {

        uint8_t type = packet[1];
        uint8_t length = packet[2];
        char *fileNameReceived;

        fileNameReceived = (char *) malloc( sizeof(char) * length );
        memcpy(fileNameReceived,packet+3,length);

        switch(type) {
            case TYPE_FILESIZE:
                // No nosso caso só recebe no fim
                fprintf(stderr, "parserPacket: Not expected fileSize type\n");
                /*appLayer.fileSize = (int) value; // fileSize is in AppLayer*/
                break;
            case TYPE_FILENAME:
                if ( appLayer.settings->status == STATUS_RECEIVER_FILE_RECEIVED_NAME ) {
                    appLayer.settings->fileName = fileNameReceived;
                    appLayer.settings->io.fptr = fopen(fileNameReceived, "w+b"); //Creates a file, if exists erases the content first
                    if (appLayer.settings->io.fptr == NULL) {
                        fprintf(stderr, "parserPacket: Error opening file '%s'\n", fileNameReceived);
                        return -1;
                    }
                }
                break;
            default:
                fprintf(stderr, "parserPacket: Start Packet type not correct\n");
                return -1;
                break;
        }
    } else if( C == C_END ) {
        uint8_t type = packet[1];
        uint8_t length = packet[2];
        /*char *fileSizeReceivedAsString;*/
        long int fileSizeReceived = 0;
        size_t i;

        switch(type) {
            case TYPE_FILESIZE:
                /*fileSizeReceivedAsString = (char *) malloc( sizeof(char) * length + 1 );*/
                /*memcpy(fileSizeReceivedAsString,packet+3,length);*/
                /*fileSizeReceivedAsString[length] = 0;*/

                for (i = 0; i < length; ++i)
                    fileSizeReceived += packet[3+i] << i*8;

                /*fprintf(stderr, "parserPacket: fileSizeReceivedAsString %.*s\n", length, fileSizeReceivedAsString);*/
                /*fileSizeReceived = atol(fileSizeReceivedAsString);*/
                fprintf(stderr, "parserPacket: fileSizeReceived %li vs fileSize %li\n", fileSizeReceived, appLayer.fileSize);
                if ( fileSizeReceived != appLayer.fileSize ) {
                    fprintf(stderr, "parserPacket: fileSizeReceived != fileSize\n");
                    return -1;
                }
                break;
            default:
                fprintf(stderr, "parserPacket: End Packet type not correct\n");
                return -1;
                break;
        }

    }
    return 0;
}

static int read(void) {
    uint8_t *packet;
    size_t packetSize;
    size_t i;

    while (1) {
        packet = llread(&packetSize);
        if ( errno != 0 ) {
            fprintf(stderr, "AppRead received llread with error\n");
            return -1;
        } else {
            if ( packet == NULL ) {
                fprintf(stderr, "AppRead received disconnect from llread\n");
                return 0;
            } else {
                fprintf(stderr, "AppRead packet: ");
                for (i = 0; i < packetSize; i++) {
                    fprintf(stderr, "%c", packet[i]);
                }
                if ( parserPacket(packet, packetSize) != 0 ) {
                    fprintf(stderr, "AppRead parserPacket failed \n");
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int write(void) {
    size_t res;
    bool end = false;
    uint8_t data[appLayer.settings->packetBodySize];
    size_t stringSize, lidos = 0;
    size_t databytesWritten = 0;

    if ( appLayer.settings->status == STATUS_TRANSMITTER_STRING )
        stringSize = strlen(appLayer.settings->io.chptr) + 1;
    else if ( appLayer.settings->status == STATUS_TRANSMITTER_FILE ) {
        // Era uma boa ideia mandar aqui um pacote de controlo só com o nome do ficheiro e no fim o tamanho do ficheiro ou da string
        // Assim já dava para usar das duas maneiras embora desse para mandar numa só tudo, no fim xor início
        if ( writeStartPacket() != 0 ) {
            fprintf(stderr, "writeStartPacket Failed\n");
            return -1;
        }
        rewind(appLayer.settings->io.fptr);
    }

    while ( !end ) {
        if ( appLayer.settings->status == STATUS_TRANSMITTER_FILE ) {
            res = fread(data, 1, appLayer.settings->packetBodySize, appLayer.settings->io.fptr);
            if ( feof(appLayer.settings->io.fptr) ) {
                fprintf(stderr, "AppWrite Reached end of file\n");
                end = true;
            } else if ( ferror(appLayer.settings->io.fptr) ) {
                fprintf(stderr, "AppWrite error occurred in fread\n");
                return -1;
            } else {
                fprintf(stderr, "AppWrite Res: %lu\n", res);
                fprintf(stderr, "AppWrite packet data: ");
                fprintf(stderr, "%.*s\n", (int)res, data);
            }
        } else if ( appLayer.settings->status == STATUS_TRANSMITTER_STRING ) {
            if ( (res = stringSize - lidos) <= appLayer.settings->packetBodySize ) {
                memcpy(data, appLayer.settings->io.chptr+lidos, res);
                fprintf(stderr, "AppWrite Reached end of string\n");
                end = true;
            } else {
                memcpy(data, appLayer.settings->io.chptr+lidos, appLayer.settings->packetBodySize);
                lidos += appLayer.settings->packetBodySize;
                res = appLayer.settings->packetBodySize;
            }
            fprintf(stderr, "AppWrite packet: %s\n", data);
        } else {
            // Por fazer, ler stream
            // Não vai chegar aqui porque retorna -1 no llinitialize
            // Fiz dessa maneira pq talvez não vai dar tempo para implementar
        }
        if ( res != 0 ) {
            if ( writeDataPacket(data, res) == -1 ) {
                fprintf(stderr, "AppWrite Number of data bytes written so far: %lu\n", databytesWritten);
                fprintf(stderr, "AppWrite failed\n");
                return -1;
            }
            databytesWritten += res;
        }
    }

    // Envia o tamanho do ficheiro ou da string
    if ( writeEndPacket() != 0 ) {
        fprintf(stderr, "writeEndPacket failed");
        return -1;
    }

    fprintf(stderr, "\n\nAppWrite Number of bytes Written: %lu\n\n", databytesWritten);
    return 0;
}

static int writeStartPacket(void) {

    size_t filenameLength = strlen(appLayer.settings->fileName) + 1;
    if ( filenameLength > 256 || filenameLength == 1 ) {
        fprintf(stderr, "writeStartPacket invalid fileName\n");
        return -1;
    }

    size_t packetSize = 3 + filenameLength; //  C + T + L + Tamanho da string(V)
    uint8_t packet[packetSize];

    packet[0] = C_START; // C
    packet[1] = TYPE_FILENAME; // T
    packet[2] = filenameLength; // V

    size_t i;

    for (i = 0; i < filenameLength; ++i) {
        packet[i+3] = appLayer.settings->fileName[i];
    }

    return llwrite(packet, packetSize);
}

static int writeDataPacket(uint8_t *data, size_t size) {
    uint8_t packet[size + 4];
    size_t i;

    if ( data == NULL || size < 5) {
        errno = EINVAL;
        return -1;
    }

    uint8_t L2 = size/256, L1 = size%256;

    fprintf(stderr, "Going to writeDataPacket\n");
    fprintf(stderr, "size: %lu L2: %d L1: %d\n", size, L2, L1);
    packet[0] = C_DATA;
    if ( appLayer.sequenceNumber == 256 ) appLayer.sequenceNumber = 0;
    packet[1] = appLayer.sequenceNumber;
    packet[2] = L2;
    packet[3] = L1;
    memcpy(packet+4, data, size);

    fprintf(stderr, "Full packet: ");
    for (i = 0; i < size+4; ++i) {
        if ( i > 3 ) fprintf(stderr, "%c", packet[i]);
        else fprintf(stderr, "%X", packet[i]);
    }
    fprintf(stderr, "\n");

    //fprintf(stderr, "Size: %d %X   L2: %d %X  L1: %d %X\n", size, size/256, size%256);
    //fprintf(stderr, "DataPacket: %s\n", packet);
    int err = llwrite(packet, size+4);
    if ( err == 0 ) {
        ++appLayer.sequenceNumber;
        return 0;
    }
    return -1;
}

static int writeEndPacket(void) {

    size_t packetSize = 3 + sizeof(appLayer.fileSize); //  C + T + L + bytes do tipo
    uint8_t packet[packetSize];

    packet[0] = C_END; // C
    packet[1] = TYPE_FILESIZE; // T
    packet[2] = sizeof(appLayer.fileSize); // V

    memcpy(packet+3,&appLayer.fileSize,sizeof(appLayer.fileSize));
    fprintf(stderr, "writeEndPacket: fileSize %li, fileSizeToSend %li\n", appLayer.fileSize, (long int)packet[3]);

    return llwrite(packet, packetSize);
}

