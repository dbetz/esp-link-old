#include <esp8266.h>
#include <osapi.h>
#include "cgi.h"
#include "cgiprop.h"
#include "serbridge.h"
#include "proploader.h"
#include "fastproploader.h"
#include "uart.h"
#include "serled.h"

#if 0
static const uint8_t blink_fast_array[] = {
 0x00, 0xb4, 0xc4, 0x04, 0x6f, 0x48, 0x10, 0x00,
 0x38, 0x00, 0x40, 0x00, 0x18, 0x00, 0x44, 0x00,
 0x28, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00, 0x00,
 0x38, 0x1a, 0xf3, 0x38, 0x1b, 0xf3, 0xea, 0x61,
 0x38, 0x1a, 0xf3, 0x3f, 0xb4, 0x60, 0x3f, 0xb6,
 0x60, 0x3f, 0xd4, 0x4b, 0x3f, 0x91, 0x35, 0xc0,
 0x37, 0x02, 0xf6, 0xec, 0x23, 0x04, 0x71, 0x32,
};

static const uint8_t blink_slow_array[] = {
 0x00, 0xb4, 0xc4, 0x04, 0x6f, 0x4a, 0x10, 0x00,
 0x38, 0x00, 0x40, 0x00, 0x18, 0x00, 0x44, 0x00,
 0x28, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00, 0x00,
 0x38, 0x1a, 0xf3, 0x38, 0x1b, 0xf3, 0xea, 0x61,
 0x38, 0x1a, 0xf3, 0x3f, 0xb4, 0x60, 0x3f, 0xb6,
 0x60, 0x3f, 0xd4, 0x4b, 0x3f, 0x91, 0x35, 0xc0,
 0x37, 0x00, 0xf6, 0xec, 0x23, 0x04, 0x71, 0x32,
};
#endif

#if 0
/* the order here must match the definition of LoadState in proploader.h */
static const char *stateNames[] = {
    "Idle",
    "Reset1",
    "Reset2",
    "TxHandshake",
    "RxHandshake",
    "VerifyChecksum",
    "StartAck",
    "Data",
    "DataAck",
    "VerifyRAMAck",
    "ProgramVerifyEEPROMAck",
    "ReadyToLaunchAck"
};
#endif

static void getLoadParameters(HttpdConnData *connData);
static void startLoading(PropellerConnection *connection, const uint8_t *image, int imageSize);
static void finishLoading(PropellerConnection *connection);
static void abortLoading(PropellerConnection *connection);
static void httpdSendResponse(HttpdConnData *connData, int code, char *message);
static void timerCallback(void *data);
static void readCallback(char *buf, short length);

#if 0
static const ICACHE_FLASH_ATTR char *stateName(LoadState state)
{
    return state >= 0 && state < stMAX ? stateNames[state] : "Unknown";
}
#endif

int8_t ICACHE_FLASH_ATTR getIntArg(HttpdConnData *connData, char *name, int *pValue)
{
  char buf[16];
  int len = httpdFindArg(connData->getArgs, name, buf, sizeof(buf));
  if (len < 0) return 0; // not found, skip
  *pValue = atoi(buf);
  return 1;
}

// this is statically allocated because the serial read callback has no context parameter
PropellerConnection myConnection;

int ICACHE_FLASH_ATTR cgiPropInit()
{
    memset(&myConnection, 0, sizeof(PropellerConnection));
    myConnection.state = stIdle;
    return 1;
}

int ICACHE_FLASH_ATTR cgiPropLoadBegin(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    PropellerImage image;
    int imageSize;
    
    if (connection->state != stIdle) {
        errorResponse(connData, 400, "Transfer already in progress\r\n");
        return HTTPD_CGI_DONE;
    }
    connData->cgiPrivData = connection;
    connection->connData = connData;
    
    if (!getIntArg(connData, "image-size", &imageSize)) {
        errorResponse(connData, 400, "image-size parameter missing\r\n");
        return HTTPD_CGI_DONE;
    }
    
    getLoadParameters(connData);
    if (!getIntArg(connData, "second-stage-baud", &connection->secondStageBaudRate))
        connection->secondStageBaudRate = 921600;
        
    DBG("load-begin: image-size %d, baud %d, second-stage-baud %d, final-baud %d\n", imageSize, connection->baudRate, connection->secondStageBaudRate, connection->finalBaudRate);
    
    if (fplGenerateInitialLoaderImage(connection, imageSize, &image) != 0) {
        errorResponse(connData, 400, "Generate loader image failed\r\n");
        return HTTPD_CGI_DONE;
    }
    
    connection->stateAfterLoadFinishes = stStartAck;
    startLoading(connection, image.imageData, image.imageSize);

    return HTTPD_CGI_MORE;
}

int ICACHE_FLASH_ATTR cgiPropLoadData(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    
    if (connection->state != stData) {
        errorResponse(connData, 400, "Not ready for a data transfer\r\n");
        abortLoading(connection);
        return HTTPD_CGI_DONE;
    }
    connData->cgiPrivData = connection;
    connection->connData = connData;
    
    if (connData->post->buffLen == 0) {
        errorResponse(connData, 400, "No data to load\r\n");
        abortLoading(connection);
        return HTTPD_CGI_DONE;
    }
    
    DBG("load-data: size %d\n", connData->post->buffLen);
    
    fplUpdateChecksum(connection, (uint8_t *)connData->post->buff, connData->post->buffLen);
    fplData(connection, (uint8_t *)connData->post->buff, connData->post->buffLen);
    
    return HTTPD_CGI_MORE;
}

int ICACHE_FLASH_ATTR cgiPropLoadEnd(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    char cmd[32];
    
    if (connection->state != stData) {
        errorResponse(connData, 400, "Not ready for a data transfer\r\n");
        abortLoading(connection);
        return HTTPD_CGI_DONE;
    }
    else if (connection->packetID != 0) {
        errorResponse(connData, 400, "More data expected\r\n");
        abortLoading(connection);
        return HTTPD_CGI_DONE;
    }
    connData->cgiPrivData = connection;
    connection->connData = connData;
    
    if (httpdFindArg(connData->getArgs, "command", cmd, sizeof(cmd)) < 0)
        os_strcpy(cmd, "run");

    DBG("load-end: command '%s'\n", cmd);

    if (os_strcmp(cmd, "run") == 0)
        connection->loadType = ltDownloadAndRun;
    else if (os_strcmp(cmd, "program-and-run") == 0)
        connection->loadType = ltDownloadAndProgramAndRun;
    else if (os_strcmp(cmd, "program") == 0)
        connection->loadType = ltDownloadAndProgram;
    else {
        errorResponse(connData, 400, "Unknown command\r\n");
        abortLoading(connection);
        return HTTPD_CGI_DONE;
    }
    
    fplVerifyRAM(connection);
    
    return HTTPD_CGI_MORE;
}

#if 0
int ICACHE_FLASH_ATTR cgiPropBlinkFast(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    
    if (connection->state != stIdle) {
        errorResponse(connData, 400, "Transfer already in progress\r\n");
        return HTTPD_CGI_DONE;
    }
    connData->cgiPrivData = connection;
    connection->connData = connData;
    
    getLoadParameters(connData);
    
    connection->stateAfterLoadFinishes = stIdle;
    startLoading(connection, blink_fast_array, sizeof(blink_fast_array));

    return HTTPD_CGI_MORE;
}

int ICACHE_FLASH_ATTR cgiPropBlinkSlow(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    
    if (connection->state != stIdle) {
        errorResponse(connData, 400, "Transfer already in progress\r\n");
        return HTTPD_CGI_DONE;
    }
    connData->cgiPrivData = connection;
    connection->connData = connData;
    
    getLoadParameters(connData);
    
    connection->stateAfterLoadFinishes = stIdle;
    startLoading(connection, blink_slow_array, sizeof(blink_slow_array));

    return HTTPD_CGI_MORE;
}
#endif

static void ICACHE_FLASH_ATTR getLoadParameters(HttpdConnData *connData)
{
    PropellerConnection *connection = (PropellerConnection *)connData->cgiPrivData;
    if (!getIntArg(connData, "initial-baud", &connection->baudRate))
        connection->baudRate = 115200;
    if (!getIntArg(connData, "final-baud", &connection->finalBaudRate))
        connection->finalBaudRate = connection->baudRate;
    if (!getIntArg(connData, "reset-pin", &connection->resetPin))
        connection->resetPin = 12;
}

static void ICACHE_FLASH_ATTR startLoading(PropellerConnection *connection, const uint8_t *image, int imageSize)
{
    connection->image = image;
    connection->imageSize = imageSize;
    
    uart0_baud(connection->baudRate);
    programmingCB = readCallback;

    makeGpio(connection->resetPin);
    GPIO_OUTPUT_SET(connection->resetPin, 1);
    connection->state = stReset1;
    
    os_timer_disarm(&connection->timer);
    os_timer_setfn(&connection->timer, timerCallback, connection);
    os_timer_arm(&connection->timer, RESET_DELAY_1, 0);
}

static void ICACHE_FLASH_ATTR finishLoading(PropellerConnection *connection)
{
    uart0_baud(connection->finalBaudRate);
    programmingCB = NULL;
    myConnection.state = stIdle;
}

static void ICACHE_FLASH_ATTR abortLoading(PropellerConnection *connection)
{
    programmingCB = NULL;
    myConnection.state = stIdle;
}

static void ICACHE_FLASH_ATTR httpdSendResponse(HttpdConnData *connData, int code, char *message)
{
    errorResponse(connData, code, message);
    httpdFlush(connData);
    connData->cgi = NULL;
}

static void ICACHE_FLASH_ATTR timerCallback(void *data)
{
    PropellerConnection *connection = (PropellerConnection *)data;
    
    os_timer_disarm(&connection->timer);
    
    switch (connection->state) {
    case stIdle:
    case stData:
        // shouldn't happen
        break;
    case stReset1:
        connection->state = stReset2;
        GPIO_OUTPUT_SET(connection->resetPin, 0);
        os_timer_arm(&connection->timer, RESET_DELAY_2, 0);
        break;
    case stReset2:
        connection->state = stTxHandshake;
        GPIO_OUTPUT_SET(connection->resetPin, 1);
        os_timer_arm(&connection->timer, RESET_DELAY_3, 0);
        break;
    case stTxHandshake:
        connection->state = stRxHandshake;
        ploadInitiateHandshake(connection);
        os_timer_arm(&connection->timer, RX_HANDSHAKE_TIMEOUT, 0);
        break;
    case stRxHandshake:
        httpdSendResponse(connection->connData, 400, "RX handshake timeout\r\n");
        abortLoading(connection);
        break;
    case stVerifyChecksum:
        if (connection->retriesRemaining > 0) {
            uart_tx_one_char(UART0, 0xF9);
            os_timer_arm(&connection->timer, connection->retryDelay, 0);
            --connection->retriesRemaining;
        }
        else {
            httpdSendResponse(connection->connData, 400, "Checksum timeout\r\n");
            abortLoading(connection);
        }
        break;
    case stStartAck:
        httpdSendResponse(connection->connData, 400, "Second-stage loader startup timeout\r\n");
        abortLoading(connection);
        break;
    case stDataAck:
        httpdSendResponse(connection->connData, 400, "Second-stage loader data timeout\r\n");
        abortLoading(connection);
        break;
    case stVerifyRAMAck:
        httpdSendResponse(connection->connData, 400, "Second-stage verify RAM timeout\r\n");
        abortLoading(connection);
        break;
    case stProgramVerifyEEPROMAck:
        httpdSendResponse(connection->connData, 400, "Second-stage program and verify EEPROM timeout\r\n");
        abortLoading(connection);
        break;
    case stReadyToLaunchAck:
        httpdSendResponse(connection->connData, 400, "Second-stage ready to launch timeout\r\n");
        abortLoading(connection);
        break;
    default:
        break;
    }
}

static void ICACHE_FLASH_ATTR readCallback(char *buf, short length)
{
    PropellerConnection *connection = &myConnection;
    int cnt, version;
    
    os_timer_disarm(&connection->timer);
    
    
    switch (connection->state) {
    case stIdle:
    case stReset1:
    case stReset2:
    case stTxHandshake:
        // just ignore data received when we're not expecting it
        break;
    case stRxHandshake:
        if ((cnt = length) > connection->bytesRemaining)
            cnt = connection->bytesRemaining;
        memcpy(&connection->buffer[connection->bytesReceived], buf, cnt);
        connection->bytesReceived += cnt;
        if ((connection->bytesRemaining -= cnt) == 0) {
            if (ploadVerifyHandshakeResponse(connection, &version) == 0) {
                if (ploadLoadImage(connection, ltDownloadAndRun, connection->image, connection->imageSize) == 0) {
                    os_timer_arm(&connection->timer, connection->retryDelay, 0);
                    connection->state = stVerifyChecksum;
                }
                else {
                    httpdSendResponse(connection->connData, 400, "Load image failed\r\n");
                    abortLoading(connection);
                }
            }
            else {
                httpdSendResponse(connection->connData, 400, "RX handshake failed\r\n");
                abortLoading(connection);
            }
        }
        break;
    case stVerifyChecksum:
        if (buf[0] == 0xFE) {
            if ((connection->state = connection->stateAfterLoadFinishes) == stIdle) {
                httpdSendResponse(connection->connData, 200, "");
                finishLoading(connection);
            }
            else {
                os_timer_arm(&connection->timer, STARTUP_TIMEOUT, 0);
                connection->bytesRemaining = sizeof(fplResponse);
                connection->bytesReceived = 0;
                connection->packetTag = 0;
            }
        }
        else {
            httpdSendResponse(connection->connData, 400, "Checksum error\r\n");
            abortLoading(connection);
        }
        break;
    case stStartAck:
    case stDataAck:
    case stVerifyRAMAck:
    case stProgramVerifyEEPROMAck:
    case stReadyToLaunchAck:
        if ((cnt = length) > connection->bytesRemaining)
            cnt = connection->bytesRemaining;
        memcpy(&connection->buffer[connection->bytesReceived], buf, cnt);
        connection->bytesReceived += cnt;
        if ((connection->bytesRemaining -= cnt) == 0) {
            if (fplGetLong(&connection->buffer[4]) != connection->packetTag) {
                char buf[80];
                os_sprintf(buf, "FPL wrong tag: expected %d, got %d, state %d\r\n",
                           (int)connection->packetTag,
                           (int)fplGetLong(&connection->buffer[4]),
//                           stateName(connection->state));
                           connection->state);
                httpdSendResponse(connection->connData, 400, buf);
                abortLoading(connection);
            }
            else if (fplGetLong(&connection->buffer[0]) != connection->expectedID) {
                char buf[80];
                os_sprintf(buf, "FPL wrong id: expected %d, got %d, state %d\r\n",
                           (int)connection->expectedID,
                           (int)fplGetLong(&connection->buffer[0]),
//                           stateName(connection->state));
                           connection->state);
                httpdSendResponse(connection->connData, 400, buf);
                abortLoading(connection);
            }
            else {
                connection->packetID = connection->expectedID;
                switch (connection->state) {
                case stStartAck:
                    uart0_baud(connection->secondStageBaudRate);
                    connection->state = stData;
                    break;
                case stDataAck:
                    connection->state = stData;
                    break;
                case stVerifyRAMAck:
                    if (connection->loadType & ltDownloadAndProgram)
                        fplProgramVerifyEEPROM(connection);
                    else
                        fplReadyToLaunch(connection);
                    break;
                case stProgramVerifyEEPROMAck:
                    fplReadyToLaunch(connection);
                    break;
                case stReadyToLaunchAck:
                    fplLaunchNow(connection);
                    finishLoading(connection);
                    break;
                default:
                    break;
                }
                httpdSendResponse(connection->connData, 200, "");
            }
        }
        break;
    default:
        break;
    }
    DBG(" --> %d\n", connection->state);
}




