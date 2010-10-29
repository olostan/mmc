/* $Id: version.c,v 1.112 2003/04/13 14:21:36 mike Exp $ */

#include "misc.h"
#ifdef WIN32
#include "config_h.win32"
#else
#include "config.h"
#endif

const char *get_version(void) {
    return "MMC v" VERSION ".0085 (" __DATE__ " " __TIME__ ") by Mike E. Matsnev";
}

