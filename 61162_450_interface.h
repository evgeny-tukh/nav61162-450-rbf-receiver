#include  "61162_450_defs.h"

#ifdef __cplusplus
namespace Nav61162_450 {
#endif

bool extractHeader (
    uint8_t *stream,        // incoming byte stream
    void *headerBuffer,     // a pointer to fill header in (will be parsed according to version field)
    uint8_t *version,       // version number; the user should assing headerBuffer to a typized pointer accordingly
    size_t *size            // number of bytes parsed; stream pointer should be shifted accodingly
);

void composeHeader_v1 (
    TokenType tokenType,
    char *srcTalker,        // EI or VR, in our case
    uint16_t srcInstance,   // 0001..9999
    char *destTalker,       // EI or VR, in our case
    uint16_t destInstance,  // 0001..9999
    uint16_t type,
    uint32_t blockID,
    uint32_t seqNum,
    uint32_t maxSeqNum,
    Header1 *header
);
void composeHeader_v2 (
    TokenType tokenType,
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
);

void composeHeaderSimple_v1 (
    TokenType tokenType,
    uint8_t *sourceID,      // immediate 6 chars here
    uint8_t *destID,        // immediate 6 chars here
    uint16_t type,
    uint32_t blockID,
    uint32_t seqNum,
    uint32_t maxSeqNum,
    Header1 *header
);
void composeHeaderSimple_v2 (
    TokenType tokenType,
    uint8_t *sourceID,      // immediate 6 chars here
    uint8_t *destID,        // immediate 6 chars here
    uint16_t type,
    uint32_t blockID,
    uint32_t seqNum,
    uint32_t maxSeqNum,
    uint8_t device,
    uint8_t channel,
    Header2 *header
);

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
);
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
);
void composeFileDesc_v1 (
    size_t fileLength,
    uint16_t ackStatus,
    uint8_t device,
    uint8_t channel,
    char *dataType,         // in MIME reparesenation
    char *statusAndInfo,
    FileDesc1 *fileDesc,
    size_t *descSize
);
void composeFileDesc_v2 (
    size_t fileLength,
    uint16_t ackStatus,
    uint16_t ackDestPort,
    char *dataType,         // in MIME reparesenation
    char *statusAndInfo,
    FileDesc2 *fileDesc,
    size_t *descSize
);

void composeAck_v1 (Header1 *incomingHdr, Header1 *ackHeader, uint32_t seqNum = 0xFFFFFFFF);
void composeAck_v2 (Header2 *incomingHdr, Header2 *ackHeader, uint32_t seqNum = 0xFFFFFFFF);

void composeFinalAck_v1 (Header1 *incomingHdr, Header1 *ackHeader);
void composeFinalAck_v2 (Header2 *incomingHdr, Header2 *ackHeader);

#ifdef __cplusplus
}
#endif
