#ifndef CHAR_H
#define	CHAR_H

#ifdef WIN32
typedef unsigned short Char;
#else
#include "config.h"
#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef STDC_HEADERS
#include <sys/types.h>
#include <stdlib.h>
#endif
#endif
typedef uint16_t Char;
#endif

#ifdef sun
#include <sys/byteorder.h>
#endif

#ifdef BYTE_ORDER
#if BYTE_ORDER == BIG_ENDIAN
#define BIG
#endif
#else
#ifdef _BIG_ENDIAN
#define BIG
#endif
#endif

#ifdef BIG
#define	CH(ch)	((((Char)(ch))>>8)&0xff)
#define	COL(ch)	(((Char)(ch))&0xff)
#define	MKCH(ch,attr) (((unsigned char)(attr))|(((unsigned short)(unsigned char)(ch))<<8))
#else
#define	COL(ch)	((((Char)(ch))>>8)&0xff)
#define	CH(ch)	(((Char)(ch))&0xff)
#define	MKCH(ch,attr) (((unsigned char)(ch))|(((unsigned short)(unsigned char)(attr))<<8))
#endif

#endif
