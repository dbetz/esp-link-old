#ifndef PROPLOADER_H
#define PROPLOADER_H

//#include <stdint.h>
#include <osapi.h>
#include "os_type.h"
#include "httpd.h"

typedef enum {
    ltShutdown = 0,
    ltDownloadAndRun = (1 << 0),
    ltDownloadAndProgram = (1 << 1),
    ltDownloadAndProgramAndRun = ltDownloadAndRun | ltDownloadAndProgram
} LoadType;

typedef enum {
    stIdle,
    stReset1,
    stReset2,
    stTxHandshake,
    stRxHandshake,
    stVerifyChecksum,
    stFastStartAck,
    stFastData,
    stFastDataAck,
    stVerifyRAMAck,
    stProgramVerifyEEPROMAck,
    stReadyToLaunchAck,
    stMAX
} LoadState;

typedef struct {
    HttpdConnData *connData;
    ETSTimer timer;
    int resetPin;
    int baudRate;
    int secondStageBaudRate;
    int finalBaudRate;
    LoadType loadType;
    const uint8_t *image;
    int imageSize;
    LoadState state;
    LoadState stateAfterLoadFinishes;
    int retriesRemaining;
    int retryDelay;
    uint8_t buffer[125 + 4]; // sizeof(rxHandshake) + 4
    int bytesReceived;
    int bytesRemaining;
    int32_t packetID;
    int32_t expectedID;
    int32_t packetTag;
    int32_t checksum;
} PropellerConnection;

#define RESET_DELAY_1           10
#define RESET_DELAY_2           10
#define RESET_DELAY_3           100
#define CALIBRATE_DELAY         10

#define RX_HANDSHAKE_TIMEOUT    2000
#define RX_CHECKSUM_TIMEOUT     250
#define EEPROM_PROGRAM_TIMEOUT  5000
#define EEPROM_VERIFY_TIMEOUT   2000

int ploadInitiateHandshake(PropellerConnection *connection);
int ploadVerifyHandshakeResponse(PropellerConnection *connection, int *pVersion);
int ploadLoadImage(PropellerConnection *connection, LoadType loadType, const uint8_t *image, int imageSize);

#endif

