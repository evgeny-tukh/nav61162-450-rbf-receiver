#ifdef _USE_STDAFX_H_
#include "stdafx.h"
#endif
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include "61162_450_defs.h"
#include "61162_450_interface.h"

bool extractHeader_v1 (uint8_t *stream, Header1 *headerBuffer);
bool extractHeader_v2 (uint8_t *stream, Header2 *headerBuffer);

bool extractHeader_v1 (uint8_t *stream, Header1 *header) {
    memcpy (header, stream, sizeof (Header1));

    header->version = 1;
    header->type = lsb2msb16 (header->type);
    header->blockID = lsb2msb32 (header->blockID);
    header->seqNum = lsb2msb32 (header->seqNum);
    header->maxSeqNum = lsb2msb32 (header->maxSeqNum);
    
    return true;
}

bool extractHeader_v2 (uint8_t *stream, Header2 *header) {
    memcpy (header, stream, sizeof (Header2));

    header->version = 2;
    header->headerLength = lsb2msb16 (sizeof (Header2));
    header->type = lsb2msb16 (header->type);
    header->blockID = lsb2msb32 (header->blockID);
    header->seqNum = lsb2msb32 (header->seqNum);
    header->maxSeqNum = lsb2msb32 (header->maxSeqNum);
    
    return true;
}

bool extractHeader (
    uint8_t *stream,    // incoming byte stream
    void *headerBuffer, // a pointer to fill header in (will be parsed according to version field)
    uint8_t *version,   // version number; the user should assing headerBuffer to a typized pointer accordingly
    size_t *size        // number of bytes parsed; stream pointer should be shifted accodingly
) {
    bool result = false;
    Header *hdr = (Header *) stream;
    uint16_t verPresent = lsb2msb16 (hdr->version);
    
    // Find out about the version
    switch (verPresent) {
        case 1: {
            extractHeader_v1 (stream, (Header1 *) headerBuffer);

            result = true;
            *version = 1;
            *size = sizeof (Header1);
            break;
        }
        case 2: {
            extractHeader_v2 (stream, (Header2 *) headerBuffer);

            result = true;
            *version = 2;
            *size = sizeof (Header2);
            break;
        }
    }

    return result;
}

void composeHeader_v1 (
    MsgTokenType tokenType,
    char *srcTalker,        // EI or VR, in our case
    uint16_t srcInstance,   // 0001..9999
    char *destTalker,       // EI or VR, in our case
    uint16_t destInstance,  // 0001..9999
    uint16_t type,
    uint32_t blockID,
    uint32_t seqNum,
    uint32_t maxSeqNum,
    Header1 *header
) {
    composeToken (header->token, tokenType);
    composeIdString (header->sourceID, srcTalker, srcInstance);
    composeIdString (header->destID, destTalker, destInstance);

    header->version = lsb2msb16 (1);
    header->type = lsb2msb16 (type);
    header->blockID = lsb2msb32 (blockID);
    header->seqNum = lsb2msb32 (seqNum);
    header->maxSeqNum = lsb2msb32 (maxSeqNum);
}

void composeHeader_v2 (
    MsgTokenType tokenType,
    char *srcTalker,        // EI or VR, in our case
    uint16_t srcInstance,   // 0001..9999
    char *destTalker,       // EI or VR, in our case
    uint16_t destInstance,  // 0001..9999
    uint16_t type,
    uint32_t blockID,
    uint32_t seqNum,
    uint32_t maxSeqNum,
    uint8_t device,
    uint8_t channel,
    Header2 *header
) {
    composeToken (header->token, tokenType);
    composeIdString (header->sourceID, srcTalker, srcInstance);
    composeIdString (header->destID, destTalker, destInstance);

    header->version = lsb2msb16 (2);
    header->type = lsb2msb16 (type);
    header->blockID = lsb2msb32 (blockID);
    header->seqNum = lsb2msb32 (seqNum);
    header->maxSeqNum = lsb2msb32 (maxSeqNum);
    header->headerLength = lsb2msb16 (sizeof (Header2));
    header->channel = channel;
    header->device = device;
}

void composeHeaderSimple_v1 (
    MsgTokenType tokenType,
    uint8_t *sourceID,      // immediate 6 chars here
    uint8_t *destID,        // immediate 6 chars here
    uint16_t type,
    uint32_t blockID,
    uint32_t seqNum,
    uint32_t maxSeqNum,
    Header1 *header
) {
    composeToken (header->token, tokenType);
    memcpy (header->sourceID, sourceID, 6);
    memcpy (header->destID, destID, 6);

    header->version = lsb2msb16 (1);
    header->type = lsb2msb16 (type);
    header->blockID = lsb2msb32 (blockID);
    header->seqNum = lsb2msb32 (seqNum);
    header->maxSeqNum = lsb2msb32 (maxSeqNum);
}

void composeHeaderSimple_v2 (
    MsgTokenType tokenType,
    uint8_t *sourceID,      // immediate 6 chars here
    uint8_t *destID,        // immediate 6 chars here
    uint16_t type,
    uint32_t blockID,
    uint32_t seqNum,
    uint32_t maxSeqNum,
    uint8_t device,
    uint8_t channel,
    Header2 *header
) {
    composeToken (header->token, tokenType);
    memcpy (header->sourceID, sourceID, 6);
    memcpy (header->destID, destID, 6);

    header->version = lsb2msb16 (2);
    header->type = lsb2msb16 (type);
    header->blockID = lsb2msb32 (blockID);
    header->seqNum = lsb2msb32 (seqNum);
    header->maxSeqNum = lsb2msb32 (maxSeqNum);
    header->headerLength = lsb2msb16 (sizeof (Header2));
    header->channel = channel;
    header->device = device;
}

void extractFileDescriptor_v1 (
    uint8_t *stream,        // incoming byte stream
    size_t *fileDescSize,   // file descriptor size; in fact, number of bytes parsed; stream pointer should be shifted accodingly
    size_t *fileSize,       // size of file transferring
    uint16_t *ackStatus,
    uint8_t *device,
    uint8_t *channel,
    char *dataType,
    size_t *dataTypeSize,   // in - buffer size, out - actual size
    char *statusInfo,
    size_t *statusInfoSize  // in - buffer size, out - actual size
) {
    FileDesc1 *fileDesc = (FileDesc1 *) stream;

    *fileDescSize = lsb2msb32 (fileDesc->length);
    *fileSize = lsb2msb32 (fileDesc->fileLength);
    *ackStatus = lsb2msb16 (fileDesc->ackStatus);
    *device = fileDesc->device;
    *channel = fileDesc->channel;

    memset (dataType, 0, *dataTypeSize);
    memcpy (dataType, fileDesc->textInfo, fileDesc->typeLength);

    *dataTypeSize = fileDesc->typeLength;

    memset (statusInfo, 0, *statusInfoSize);

    *statusInfoSize = *fileDescSize - ((fileDesc->textInfo - (char *) fileDesc) + *dataTypeSize);

    memcpy (statusInfo, fileDesc->textInfo + *dataTypeSize, *statusInfoSize);
}

void extractFileDescriptor_v2 (
    uint8_t *stream,        // incoming byte stream
    size_t *fileDescSize,   // file descriptor size; in fact, number of bytes parsed; stream pointer should be shifted accodingly
    size_t *fileSize,       // size of file transferring
    uint16_t *ackStatus,
    uint16_t *ackDestPort,
    char *dataType,
    size_t *dataTypeSize,   // in - buffer size, out - actual size
    char *statusInfo,
    size_t *statusInfoSize  // in - buffer size, out - actual size
) {
    FileDesc2 *fileDesc = (FileDesc2 *) stream;

    *fileDescSize = lsb2msb32 (fileDesc->length);
    *fileSize = lsb2msb32 (fileDesc->fileLength);
    *ackStatus = lsb2msb16 (fileDesc->ackStatus);
    *ackDestPort = lsb2msb16 (fileDesc->ackDestPort);

    memset (dataType, 0, *dataTypeSize);
    memcpy (dataType, fileDesc->textInfo, fileDesc->typeLength);

    *dataTypeSize = fileDesc->typeLength;

    uint16_t *srcStatusLength = (uint16_t *) (fileDesc->textInfo + fileDesc->typeLength);

    memset (statusInfo, 0, *statusInfoSize);
    *statusInfoSize = lsb2msb16 (*srcStatusLength);
    memcpy (statusInfo, srcStatusLength + 1, *statusInfoSize);
}

void composeFileDesc_v1 (
    size_t fileLength,
    uint16_t ackStatus,
    uint8_t device,
    uint8_t channel,
    char *dataType,         // in MIME reparesenation
    char *statusAndInfo,
    FileDesc1 *fileDesc,
    size_t *descSize
) {
    size_t dataTypeLen = strlen (dataType);
    size_t statusAndInfoLen = strlen (statusAndInfo);

    *descSize = sizeof (*fileDesc) + dataTypeLen + statusAndInfoLen + 1;

    fileDesc->length = lsb2msb32 (*descSize);
    fileDesc->fileLength = lsb2msb32 (fileLength);
    fileDesc->ackStatus = lsb2msb16 (ackStatus);
    fileDesc->device = device;
    fileDesc->channel = channel;
    fileDesc->typeLength = dataTypeLen + 1;

    memcpy (fileDesc->textInfo, dataType, dataTypeLen + 1);
    memcpy (fileDesc->textInfo + dataTypeLen + 1, statusAndInfo, statusAndInfoLen + 1);
}

void composeFileDesc_v2 (
    size_t fileLength,
    uint16_t ackStatus,
    uint16_t ackDestPort,
    char *dataType,         // in MIME reparesenation
    char *statusAndInfo,
    FileDesc2 *fileDesc,
    size_t *descSize
) {
    size_t dataTypeLen = strlen (dataType);
    size_t statusAndInfoLen = strlen (statusAndInfo);

    *descSize = sizeof (*fileDesc) + dataTypeLen + sizeof (uint16_t) + statusAndInfoLen + 1;

    fileDesc->length = lsb2msb32 (*descSize);
    fileDesc->fileLength = lsb2msb32 (fileLength);
    fileDesc->ackStatus = lsb2msb16 (ackStatus);
    fileDesc->ackDestPort = lsb2msb16 (ackDestPort);
    fileDesc->typeLength = dataTypeLen + 1;

    uint16_t *statusLength = (uint16_t *) (fileDesc->textInfo + fileDesc->typeLength);

    *statusLength = lsb2msb16 (statusAndInfoLen + 1);

    memcpy (fileDesc->textInfo, dataType, dataTypeLen + 1);
    memcpy (statusLength + 1, statusAndInfo, statusAndInfoLen + 1);
}

void composeAck_v1 (Header1 *incomingHdr, Header1 *ackHeader, uint32_t seqNum) {
    composeHeaderSimple_v1 (
        getTokenType (incomingHdr->token),
        incomingHdr->destID,
        incomingHdr->sourceID,
        MsgType::ACK,
        incomingHdr->blockID,
        seqNum == 0xFFFFFFFF ? incomingHdr->seqNum : seqNum,
        0, // No max seq no for acks
        ackHeader
    );
}

void composeFinalAck_v1 (Header1 *incomingHdr, Header1 *ackHeader) {
    composeAck_v1 (incomingHdr, ackHeader);
}

void composeAck_v2 (Header2 *incomingHdr, Header2 *ackHeader, uint32_t seqNum) {
    composeHeaderSimple_v2 (
        getTokenType (incomingHdr->token),
        incomingHdr->destID,
        incomingHdr->sourceID,
        MsgType::ACK,
        incomingHdr->blockID,
        seqNum == 0xFFFFFFFF ? incomingHdr->seqNum : seqNum,
        0, //incomingHdr->maxSeqNum, // No max seq num for ack
        incomingHdr->device,
        incomingHdr->channel,
        ackHeader
    );
}

void composeFinalAck_v2 (Header2 *incomingHdr, Header2 *ackHeader) {
    composeAck_v2 (incomingHdr, ackHeader);

    ackHeader->maxSeqNum = 0;
}
