#ifndef PROPLOADER_H
#define PROPLOADER_H

//#include <stdint.h>
#include <osapi.h>
#include "os_type.h"
#include "httpd.h"

#define PROP_DBG

#ifdef PROP_DBG
#define DBG(format, ...) os_printf(format, ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

typedef enum {
    ltShutdown = 0,
    ltDownloadAndRun = (1 << 0),
    ltDownloadAndProgram = (1 << 1),
    ltDownloadAndProgramAndRun = ltDownloadAndRun | ltDownloadAndProgram
} LoadType;

typedef enum {
/*  0 */    stIdle,
/*  1 */    stReset1,
/*  2 */    stReset2,
/*  3 */    stTxHandshake,
/*  4 */    stRxHandshake,
/*  5 */    stVerifyChecksum,
/*  6 */    stStartAck,
/*  7 */    stData,
/*  8 */    stDataAck,
/*  9 */    stVerifyRAMAck,
/* 10 */    stProgramVerifyEEPROMAck,
/* 11 */    stReadyToLaunchAck,
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

