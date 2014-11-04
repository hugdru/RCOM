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

    if(bundle == NULL) {
        fprintf(stderr, "Error: bundle is null\n");
        errno = EINVAL;
        return 1;
    }

    appLayer.settings = &bundle->alSettings;
    appLayer.fileSize = 0;

    if(appLayer.settings->status == STATUS_TRANSMITTER_FILE) {
        if( fseek(appLayer.settings->io.fptr, 0, SEEK_END) ){
            fclose(appLayer.settings->io.fptr);
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
    }

    llinitialize(&(bundle->llSettings), IS_RECEIVER(appLayer.settings->status));

    if( llopen() < 0) {
        fprintf(stderr, "Error: llopen()\n");
        return -1;
    }
    else fprintf(stderr, "llopen() was successful\n");

    if(IS_RECEIVER(appLayer.settings->status)) {
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

    if( llclose() < 0) {
        fprintf(stderr, "Error: llclose()\n");
        return -1;
    }
    else fprintf(stderr, "llclose() was successful\n");

    return 0;
}

static int parserPacket(uint8_t* packet, size_t size) {
    uint8_t C = packet[0];
    int res;
   //if(C == C_DATA) {
        uint8_t L2 = packet[2];
        uint8_t L1 = packet[3];
        uint32_t dataSize = 256 * L2 + L1;

        if(appLayer.settings->status == STATUS_RECEIVER_FILE)
                res = fwrite(packet+4, 1, dataSize, appLayer.settings->io.fptr);

        fprintf(stderr, "ParserPacket Res: %d\n", res);


    //}

     if(C == C_START) {
        size_t i = 1;
        while(i < size) {
            uint8_t type = packet[i++];
            size_t length = packet[i++];
            uint8_t value[length];


            strncpy (value, packet + i, length);
            i += length;

            switch(type) {
                case TYPE_FILESIZE:
                    appLayer.fileSize = (int) value; // fileSize is in AppLayer
                    break;
                case TYPE_FILENAME:
                    appLayer.settings->fileName = value;
                    appLayer.settings->io.fptr = fopen(value, "w"); //Creates a file, if exists erases the content first
                    if (appLayer.settings->io.fptr == NULL) {
                        fprintf(stderr, "Error opening file '%s'\n", value);
                        exit(1);
                    }
                    break;
                default:
                    fprintf(stderr, "Error: Start Packet type not correct\n");
                    exit(1);
                    break;
            }

        }
    }
    else if(C == C_END) {

        llclose();
        return 1;
    }
    return 0;
}

static int read(void) {
    uint8_t *packet;
    size_t packetSize;
    size_t i;

    while(1) {
        packet = llread(&packetSize);
        if ( errno != 0 ) {
            fprintf(stderr, "AppRead errno\n");
            return -1;
        } else {
            if ( packet == NULL ) {
                fprintf(stderr, "AppRead NULL\n");
                return 0;
            } else {
                fprintf(stderr, "AppRead packet: ");
                for(i = 0; i < packetSize; i++) {
                    fprintf(stderr, "%c", packet[i]);
                }
                parserPacket(packet, packetSize);
            }
        }
    }
    return 0;
}

static int write(void) {
    size_t res;
    bool end = false;
    uint8_t data[appLayer.settings->packetBodySize+10];
    size_t stringSize, lidos = 0;
    int numCharWritted = 0;

    if(appLayer.settings->status == STATUS_TRANSMITTER_STRING)
        stringSize = strlen(appLayer.settings->io.chptr);
   else {
        // writeStartPacket();
        rewind(appLayer.settings->io.fptr);
   }

    while(!end) {
        if(appLayer.settings->status == STATUS_TRANSMITTER_FILE) {
            res = fread(data, 1, appLayer.settings->packetBodySize, appLayer.settings->io.fptr);
            fprintf(stderr, "AppWrite Res: %d\n", res);
            fprintf(stderr, "AppWrite packet: %s\n", data);
            if(res < appLayer.settings->packetBodySize)
                end = true; // End of file
        }

        else if(appLayer.settings->status == STATUS_TRANSMITTER_STRING) {
            if(stringSize - lidos <  appLayer.settings->packetBodySize) {
                memcpy(data, appLayer.settings->io.chptr+lidos, stringSize-lidos);
                end = true;
            }
            else {
                memcpy(data, appLayer.settings->io.chptr+lidos, appLayer.settings->packetBodySize);
                lidos += appLayer.settings->packetBodySize;
            }
            fprintf(stderr, "AppWrite packet: %s\n", data);

        }

        else {
            //Por fazer, ler stream
        }

        if( writeDataPacket(data, res) == 1) {
             fprintf(stderr, "AppWrite Number of chars read so far: %d\n", numCharWritted);
             fprintf(stderr, "AppWrite failed\n");
             return -1;
        }
        numCharWritted += res;
    }

    if(appLayer.settings->status == STATUS_TRANSMITTER_FILE)
        writeEndPacket();

    fprintf(stderr, "\n\nAppWrite Number of chars Writted: %d\n\n", numCharWritted);
    return 0;
}

static int writeStartPacket(void) {
    size_t filenameLength = strlen(appLayer.settings->fileName);

    size_t packetSize = filenameLength + 9; //1 byte do type, 2 para cada parametro para o TL e fileSizeLength e filenameLength
    uint8_t packet[packetSize];

    //sprintf(packet, "%02x:%02x:%d%d%02x:%02zu:%s", C_START, TYPE_FILESIZE, (int) sizeof(int), appLayer.fileInfo->fileSize, TYPE_FILENAME, filenameLength, appLayer.fileInfo->fileName);

    return llwrite(packet, packetSize);
}

static int writeEndPacket(void) {
    uint8_t packet = C_END;

    return llwrite(&packet, 1);
}

static int writeDataPacket(uint8_t *data, size_t size) {
    uint8_t packet[appLayer.settings->packetBodySize + 4];

    sprintf(packet,"%X%X%X%X%s", C_DATA, 1, size/256, size%256, data);
    fprintf(stderr, "DataPacket: %s\n", packet);
    return llwrite(packet, strlen(packet));
}


