#ifdef _USE_STDAFX_H_
#include "stdafx.h"
#endif
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <stdio.h>
#include "61162_450_defs.h"

#define TITLE_GAP   "\n***********************"

FILE *openLog (char *);

uint16_t lsb2msb16 (uint16_t value) {
    uint8_t *bytes = (uint8_t *) & value;
    
    uint8_t b0 = bytes[0];
    uint8_t b1 = bytes[1];
    
    bytes[0] = b1;
    bytes[1] = b0;

    return value;
}

uint32_t lsb2msb32 (uint32_t value) {
    uint8_t *bytes = (uint8_t *) & value;
    
    uint8_t b0 = bytes[0];
    uint8_t b1 = bytes[1];
    uint8_t b2 = bytes[2];
    uint8_t b3 = bytes[3];
    
    bytes[0] = b3;
    bytes[1] = b2;
    bytes[2] = b1;
    bytes[3] = b0;

    return value;
}

void composeToken (IdString dest, MsgTokenType tokenType) {
    switch (tokenType) {
        case sentence:
            memcpy (dest, "UdPbC", 6); break;
        case pgnMessage:
            memcpy (dest, "NkPgN", 6); break;
        case binaryFile:
            memcpy (dest, "RaUdP", 6); break;
        case retransmittableBinaryFile:
            memcpy (dest, "RrUdP", 6); break;
        default:
            memset (dest, 0, 6);
    }
}

void composeIdString (IdString dest, char *talker, uint16_t instance) {
    memcpy (dest, talker, 2);
    sprintf (((char *) dest) + 2, "%04d", instance);
}

FILE *openLog (char *path) {
    static char savedPath [1000];

    if (path) strcpy (savedPath, path);

    FILE *log = fopen (savedPath, "rb+");

    if (!log) log = fopen (savedPath, "wb+");

    if (log) fseek (log, 0, SEEK_END);

    return log;
}

void addTextToDump (char *text) {
    FILE *log = openLog (0);
    
    if (log) {
        fwrite (text, 1, strlen (text), log);
        fclose (log);
    }
}

void dump (char *path, uint8_t *data, size_t size, bool addTitle) {
    FILE *log = openLog (path);
    if (!log) return;

    if (addTitle) {
        char title [200];
        sprintf (
            title,
            TITLE_GAP
            "\nReceived %zd bytes"
            TITLE_GAP
            "\n",
            size
        );
        fwrite (title, 1, strlen (title), log);
    }

    for (size_t offset = 0; offset < size; ) {
        char ascii[100], hex[100];
        
        memset (ascii, 0, sizeof (ascii));
        memset (hex, 0, sizeof (hex));

        for (size_t i = 0; i < 16 && offset < size; ++ i) {
            if (data[offset] >= 32 && data[offset] < 127) {
                ascii[i] = data[offset];
            } else {
                ascii[i] = '.';
            }

            char buffer [50];
            sprintf (buffer, " %02X", data[offset]);
            strcat (hex, buffer);

            ++ offset;
        }

        for (size_t i = strlen (ascii); i < 16; ascii[i++] = ' ');

        fwrite (ascii, 1, strlen (ascii), log);
        fwrite (hex, 1, strlen (hex), log);
        fwrite ("\r\n", 1, 2, log);
    }

    fclose (log);
}

