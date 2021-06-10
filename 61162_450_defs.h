#pragma once

#pragma pack(1)

#include <stdint.h>

typedef unsigned char IdString[6];

enum MsgTokenType {
    sentence,
    binaryFile,
    retransmittableBinaryFile,
    pgnMessage,

    unknown = 99
};

enum Channel {
    EcdisImage = 1,
    ChartInfo = 2,
    Route = 3,
};

// Generic header structure, common for v1 and v2
// Needed to have an access to version field
struct Header {
    IdString token;
    uint16_t version;
};

struct Header1: Header {
    IdString sourceID;
    IdString destID;
    uint16_t type;
    uint32_t blockID;
    uint32_t seqNum;
    uint32_t maxSeqNum;
};

struct Header2: Header {
    uint16_t headerLength;
    IdString sourceID;
    IdString destID;
    uint16_t type;
    uint32_t blockID;
    uint32_t seqNum;
    uint32_t maxSeqNum;
    uint8_t device;
    uint8_t channel;
};

enum MsgType {
    DATA = 1,
    QUERY = 2,
    ACK = 3,
};

struct FileDesc {
    uint32_t length;
    uint32_t fileLength;
    uint16_t ackStatus;
};

struct FileDesc1: FileDesc {
    uint8_t device;
    uint8_t channel;
    uint8_t typeLength;
    char textInfo[1];       // data type, status, info text
};

struct FileDesc2: FileDesc {
    uint16_t ackDestPort;
    uint8_t typeLength;
    char textInfo[1];       // data type, status length, status, info text
};

// least significant byte <=> most significant byte conversion
uint16_t lsb2msb16 (uint16_t);
uint32_t lsb2msb32 (uint32_t);

void composeToken (IdString dest, MsgTokenType tokenType);
void composeIdString (IdString dest, char *talker, uint16_t instance);

void dump (char *path, uint8_t *data, size_t size, bool addTitle = true);
void addTextToDump (char *);

inline const MsgTokenType getTokenType (uint8_t *token) {
    if (memcmp (token, "UdPbC", 5) == 0) return MsgTokenType::sentence;
    if (memcmp (token, "RaUdP", 5) == 0) return MsgTokenType::binaryFile;
    if (memcmp (token, "RrUdP", 5) == 0) return MsgTokenType::retransmittableBinaryFile;
    if (memcmp (token, "NkPgN", 5) == 0) return MsgTokenType::pgnMessage;
    return MsgTokenType::unknown;
}

inline const char *getTokenTypeName (MsgTokenType type) {
    switch (type) {
        case MsgTokenType::sentence: return "formatted sentence";
        case MsgTokenType::binaryFile: return "binary file transafer";
        case MsgTokenType::retransmittableBinaryFile: return "retransmittable binary file transafer";
        case MsgTokenType::pgnMessage: return "PGN message";
        default: return "unknwon";
    }
}

inline const char *getMsgTypeName (MsgType type) {
    switch (type) {
        case MsgType::DATA: return "data";
        case MsgType::QUERY: return "query";
        case MsgType::ACK: return "ack";
        default: return "unknown";
    }
}

#pragma pack()