#include <esp8266.h>
#include <stdlib.h>
#include <math.h>
#include "proploader.h"
#include "fastproploader.h"
#include "propimage.h"
#include "uart.h"

#define FAILSAFE_TIMEOUT    2.0         /* Number of seconds to wait for a packet from the host */
#define MAX_RX_SENSE_ERROR  23          /* Maximum number of cycles by which the detection of a start bit could be off (as affected by the Loader code) */
// size of data buffer in the second-stage loader
#define MAX_PACKET_SIZE     1024


// Offset (in bytes) from end of Loader Image pointing to where most host-initialized values exist.
// Host-Initialized values are: Initial Bit Time, Final Bit Time, 1.5x Bit Time, Failsafe timeout,
// End of Packet timeout, and ExpectedID.  In addition, the image checksum at word 5 needs to be
// updated.  All these values need to be updated before the download stream is generated.
// NOTE: DAT block data is always placed before the first Spin method
#define RAW_LOADER_INIT_OFFSET_FROM_END (-(10 * 4) - 8)

// Raw loader image.  This is a memory image of a Propeller Application written in PASM that fits into our initial
// download packet.  Once started, it assists with the remainder of the download (at a faster speed and with more
// relaxed interstitial timing conducive of Internet Protocol delivery. This memory image isn't used as-is; before
// download, it is first adjusted to contain special values assigned by this host (communication timing and
// synchronization values) and then is translated into an optimized Propeller Download Stream understandable by the
// Propeller ROM-based boot loader.
#include "IP_Loader.h"

static uint8_t initCallFrame[] = {0xFF, 0xFF, 0xF9, 0xFF, 0xFF, 0xFF, 0xF9, 0xFF};

double ClockSpeed = 80000000.0;

int ICACHE_FLASH_ATTR fplGenerateInitialLoaderImage(PropellerConnection *connection, int imageSize, PropellerImage *image)
{
    int initAreaOffset = sizeof(rawLoaderImage) + RAW_LOADER_INIT_OFFSET_FROM_END;
    int initialBaudRate = connection->baudRate;
    int finalBaudRate = connection->secondStageBaudRate;
    int i;
    
    connection->expectedID = (imageSize + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE;
    
    connection->checksum = 0;
    for (i = 0; i < (int)sizeof(initCallFrame); ++i)
        connection->checksum += initCallFrame[i];    
 
    // Make an image from the loader template
    pimageSetImage(image, rawLoaderImage, sizeof(rawLoaderImage));
 
    // Clock mode
    //PropellerImageSetLong(image, initAreaOffset +  0, 0);

    // Initial Bit Time.
    pimageSetLong(image, initAreaOffset +  4, (int)trunc(80000000.0 / initialBaudRate + 0.5));

    // Final Bit Time.
    pimageSetLong(image, initAreaOffset +  8, (int)trunc(80000000.0 / finalBaudRate + 0.5));

    // 1.5x Final Bit Time minus maximum start bit sense error.
    pimageSetLong(image, initAreaOffset + 12, (int)trunc(1.5 * ClockSpeed / finalBaudRate - MAX_RX_SENSE_ERROR + 0.5));

    // Failsafe Timeout (seconds-worth of Loader's Receive loop iterations).
    pimageSetLong(image, initAreaOffset + 16, (int)trunc(FAILSAFE_TIMEOUT * ClockSpeed / (3 * 4) + 0.5));

    // EndOfPacket Timeout (2 bytes worth of Loader's Receive loop iterations).
    pimageSetLong(image, initAreaOffset + 20, (int)trunc((2.0 * ClockSpeed / finalBaudRate) * (10.0 / 12.0) + 0.5));

    // PatchLoaderLongValue(RawSize*4+RawLoaderInitOffset + 24, Max(Round(ClockSpeed * SSSHTime), 14));
    // PatchLoaderLongValue(RawSize*4+RawLoaderInitOffset + 28, Max(Round(ClockSpeed * SCLHighTime), 14));
    // PatchLoaderLongValue(RawSize*4+RawLoaderInitOffset + 32, Max(Round(ClockSpeed * SCLLowTime), 26));

    // Minimum EEPROM Start/Stop Condition setup/hold time (400 KHz = 1/0.6 µS); Minimum 14 cycles
    //pimageSetLong(image, initAreaOffset + 24, 14);

    // Minimum EEPROM SCL high time (400 KHz = 1/0.6 µS); Minimum 14 cycles
    //pimageSetLong(image, initAreaOffset + 28, 14);

    // Minimum EEPROM SCL low time (400 KHz = 1/1.3 µS); Minimum 26 cycles
    //pimageSetLong(image, initAreaOffset + 32, 26);

    // First Expected Packet ID; total packet count.
    pimageSetLong(image, initAreaOffset + 36, connection->expectedID);

    // Recalculate and update checksum so low byte of checksum calculates to 0.
    pimageUpdateChecksum(image);

    /* return successfully */
    return 0;
}

static void ICACHE_FLASH_ATTR TransmitPacket(PropellerConnection *connection, uint8_t *payload, int payloadSize, int timeout)
{
    uint8_t hdr[8];

    /* initialize the packet header */
    connection->packetTag = (int32_t)rand();
    fplSetLong(&hdr[0], connection->packetID);
    fplSetLong(&hdr[4], connection->packetTag);
        
    /* send the header and data */
    uart0_tx_buffer((char *)hdr, sizeof(hdr));
    uart0_tx_buffer((char *)payload, payloadSize);
    
    /* setup to receive the ack */
    if (timeout > 0) {
        os_timer_arm(&connection->timer, timeout, 0);
        connection->bytesRemaining = sizeof(fplResponse);
        connection->bytesReceived = 0;
    }
}

void ICACHE_FLASH_ATTR fplData(PropellerConnection *connection, uint8_t *payload, int payloadSize)
{
    TransmitPacket(connection, payload, payloadSize, PACKET_TIMEOUT);
    connection->expectedID = connection->packetID - 1;
    connection->state = stFastDataAck;
}

void ICACHE_FLASH_ATTR fplUpdateChecksum(PropellerConnection *connection, uint8_t *payload, int payloadSize)
{
    int i;
    for (i = 0; i < payloadSize; ++i)
        connection->checksum += payload[i];
}
    
void ICACHE_FLASH_ATTR fplVerifyRAM(PropellerConnection *connection)
{
    TransmitPacket(connection, verifyRAM, sizeof(verifyRAM), PACKET_TIMEOUT);
    connection->expectedID = -connection->checksum;
    connection->state = stVerifyRAMAck;
}

void ICACHE_FLASH_ATTR fplProgramVerifyEEPROM(PropellerConnection *connection)
{
    TransmitPacket(connection, programVerifyEEPROM, sizeof(programVerifyEEPROM), FLASH_TIMEOUT);
    connection->expectedID = -connection->checksum * 2;
    connection->state = stProgramVerifyEEPROMAck;
}

void ICACHE_FLASH_ATTR fplReadyToLaunch(PropellerConnection *connection)
{
    TransmitPacket(connection, readyToLaunch, sizeof(readyToLaunch), PACKET_TIMEOUT);
    connection->expectedID = connection->packetID - 1;
    connection->state = stReadyToLaunchAck;
}

void ICACHE_FLASH_ATTR fplLaunchNow(PropellerConnection *connection)
{
    TransmitPacket(connection, launchNow, sizeof(launchNow), PACKET_TIMEOUT);
    connection->state = stIdle;
}

int32_t ICACHE_FLASH_ATTR fplGetLong(const uint8_t *buf)
{
     return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

void ICACHE_FLASH_ATTR fplSetLong(uint8_t *buf, uint32_t value)
{
     buf[3] = value >> 24;
     buf[2] = value >> 16;
     buf[1] = value >>  8;
     buf[0] = value;
}

