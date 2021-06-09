#ifdef _USE_STDAFX_H_
#include "stdafx.h"
#endif
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <stdio.h>
#include "61162_450_defs.h"
#include <Windows.h>

#define TITLE_GAP   "\n***********************"

namespace Nav61162_450 {
    HANDLE openLog (char *, bool);
}

uint16_t Nav61162_450::lsb2msb16 (uint16_t value) {
    uint8_t *bytes = (uint8_t *) & value;
    
    uint8_t b0 = bytes[0];
    uint8_t b1 = bytes[1];
    
    bytes[0] = b1;
    bytes[1] = b0;

    return value;
}

uint32_t Nav61162_450::lsb2msb32 (uint32_t value) {
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

void Nav61162_450::composeToken (IdString dest, Nav61162_450::TokenType tokenType) {
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

void Nav61162_450::composeIdString (IdString dest, char *talker, uint16_t instance) {
    memcpy (dest, talker, 2);
    sprintf (((char *) dest) + 2, "%04d", instance);
}

HANDLE Nav61162_450::openLog (char *path, bool wipeFile) {
    static HANDLE log = INVALID_HANDLE_VALUE;
    
    if (log == INVALID_HANDLE_VALUE) {
        static char savedPath [1000];

        if (path) strcpy (savedPath, path);

        log = CreateFile (savedPath, GENERIC_WRITE, 0, 0, wipeFile ? CREATE_ALWAYS : OPEN_ALWAYS, 0, 0);

        if (log != INVALID_HANDLE_VALUE) SetFilePointer (log, 0, 0, SEEK_END);
    }

    return log;
}

void Nav61162_450::addTextToDump (char *text) {
    HANDLE log = openLog (0, false);
    
    if (log != INVALID_HANDLE_VALUE) {
        unsigned long bytesWritten;
        WriteFile (log, text, (unsigned long) strlen (text), & bytesWritten, 0);
        //fclose (log);
    }
}

void Nav61162_450::cleanupLog (char *path) {
    openLog (path, true);
}

void Nav61162_450::dump (char *path, uint8_t *data, size_t size, bool addTitle) {
    HANDLE log = openLog (path, false);
    unsigned long bytesWritten;
    if (log == INVALID_HANDLE_VALUE) return;

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
        WriteFile (log, title, (unsigned long) strlen (title), & bytesWritten, 0);
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

        WriteFile (log, ascii, (unsigned long) strlen (ascii), & bytesWritten, 0);
        WriteFile (log, hex, (unsigned long) strlen (hex), & bytesWritten, 0);
        WriteFile (log, "\r\n", 2, & bytesWritten, 0);
    }

    //fclose (log);
}

