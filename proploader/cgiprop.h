#ifndef CGIPROP_H
#define CGIPROP_H

#include <httpd.h>

int cgiPropLoadBegin(HttpdConnData *connData);
int cgiPropLoadData(HttpdConnData *connData);
int cgiPropLoadEnd(HttpdConnData *connData);
int cgiPropBlinkFast(HttpdConnData *connData);
int cgiPropBlinkSlow(HttpdConnData *connData);

#endif

