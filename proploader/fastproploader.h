#ifndef FASTPROPLOADER_H
#define FASTPROPLOADER_H

//#include <stdint.h>
#include "os_type.h"

#include "propimage.h"
#include "proploader.h"

#define STARTUP_TIMEOUT     2000
#define PACKET_TIMEOUT      2000
#define FLASH_TIMEOUT       8000

typedef struct {
    uint32_t data[2];
} fplResponse;

int fplGenerateInitialLoaderImage(PropellerConnection *connection, int imageSize, PropellerImage *image);
void fplData(PropellerConnection *connection, uint8_t *payload, int payloadSize);
void fplUpdateChecksum(PropellerConnection *connection, uint8_t *payload, int payloadSize);
void fplVerifyRAM(PropellerConnection *connection);
void fplProgramVerifyEEPROM(PropellerConnection *connection);
void fplReadyToLaunch(PropellerConnection *connection);
void fplLaunchNow(PropellerConnection *connection);

int32_t fplGetLong(const uint8_t *buf);
void fplSetLong(uint8_t *buf, uint32_t value);

#endif
