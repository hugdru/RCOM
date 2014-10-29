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
    unsigned char * fileName;
} FileInfo;

typedef struct {
    FileInfo * fileInfo;
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
static void write(void);


int initAppLayer(Bundle *bundle) {

    if(bundle == NULL) {
        fprintf(stderr, "Error: bundle is null\n");
        return 1;
    }


    appLayer.settings = &bundle->alSettings;

    if(appLayer.settings->status == STATUS_TRANSMITTER_FILE) {
        if(appLayer.settings->io.fptr == NULL) {
                fprintf(stderr, "Error opening file '%s'\n", appLayer.fileInfo->fileName);
                return -1;
        }

        if( fseek(appLayer.settings->io.fptr, 0, SEEK_END) ){
             fclose(appLayer.settings->io.fptr);
             fprintf(stderr, "Error: Cant's find file '%s' size", appLayer.fileInfo->fileName);
             return -1;
        }
        long int fileSize = ftell(appLayer.settings->io.fptr);
    }

    llinitialize(&(bundle->llSettings), IS_RECEIVER(appLayer.settings->status));

    if( llopen() < 0) {
        fprintf(stderr, "Error: llopen()\n");
        exit(1);
    }

    fprintf(stderr, "After llopen()\n");
    if(IS_RECEIVER(appLayer.settings->status))
        read();
    else write();

    return 0;
}

static int parserPacket(uint8_t* packet, size_t size) {
    uint8_t C = packet[0];

    if(C == C_DATA) {
        uint8_t L2 = packet[2];
        uint8_t L1 = packet[3];
        uint32_t dataSize = 256 * L2 + L1;

        size_t i;
        for(i = 0; i < dataSize; i++) {
            if(appLayer.settings->status == STATUS_RECEIVER_FILE)
                fprintf(appLayer.settings->io.fptr, "%c", packet[4+i]);

            else if(appLayer.settings->status == STATUS_RECEIVER_STREAM)
                fprintf(stderr, "%c", packet[4+i]);
        }
    }

    else if(C == C_START) {
        size_t i = 1;
        while(i < size) {
            uint8_t type = packet[i++];
            size_t length = packet[i++];
            uint8_t value[length];


            strncpy (value, packet + i, length);
            i += length;

            switch(type) {
                case TYPE_FILESIZE:
                    appLayer.fileInfo->fileSize = (int) value; // fileSize is in AppLayer
                    break;
                case TYPE_FILENAME:
                    appLayer.fileInfo->fileName = value;
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
        uint16_t packetSize;
        int end = 0;
        bool disconnected = false;

        while(!end) {
            packet = llread(&packetSize);
            if ( errno != 0 ) return -1;
            else {
                if ( packet == NULL ) return -1;
                if ( errno == 0 && packet == NULL ) {
                // Disconnect received, will not receive more packets
                disconnected = true;
                } else {
                // Ainda não acabou
                end = parserPacket(packet, packetSize);

                }
            }
        }
        return 0;
}

static int writeStartPacket(void) {
    size_t filenameLength = strlen(appLayer.fileInfo->fileName);

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

    //sprintf(packet,"%c%c%c%c%s", C_DATA, 1, size/256, size%256, data);
    return llwrite(packet, strlen(packet));
}

static void write(void) {
    size_t res;
    bool end = false;
    uint8_t data[appLayer.settings->packetBodySize];

    writeStartPacket();

    while(!end) {
        if(appLayer.settings->status == STATUS_TRANSMITTER_FILE)
            res = fread(data, 1, appLayer.settings->packetBodySize, appLayer.settings->io.fptr);
        else if(appLayer.settings->status == STATUS_TRANSMITTER_STRING) {
            int jaLidos = 10; //falta adicionar variavel a incrementar sempre que é lido mais x caracteres
            strncpy(data, appLayer.settings->io.chptr+jaLidos, appLayer.settings->packetBodySize);
        }
        else {
            //Por fazer, ler stream
        }

        if(res < appLayer.settings->packetBodySize)
            end = true; // End of file
        writeDataPacket(data, res);
    }

    writeEndPacket();
}

