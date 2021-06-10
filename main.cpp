#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>
#include <WinSock.h>
#include <Shlwapi.h>
#include <stdint.h>
#include <time.h>
#include <vector>

#include "61162_450_defs.h"
#include "61162_450_interface.h"

enum ErrorFlags {
    SUPPRESS_FINAL_ACK = 1,
};

struct Cfg {
    uint16_t port;
    uint32_t flags;
    int fakeAckAfterChunk, numberOfFakeAcks;
    char ourID [7];
    bool stopWhenRetransmission, openImage;
    time_t timeout;
    char group [50], bind [50];
};

struct Ctx {
    Cfg config;
    uint32_t blockID;
    uint32_t lastSeqNo, maxSeqNo;
    uint8_t token [6], sender [6], dest [6];
    uint16_t ackDestPort;
    SOCKET sock;
    sockaddr_in lastSender;
    HANDLE binaryFile;
    char mime [100];
    int lastChunkProcessed;
    std::vector<size_t> chunkOffsets;
    bool retransmissionExpected;
    int addZerosAfter;
};

const char *SECTION_CFG = "cfg";
const char *KEY_PORT = "port";
const char *KEY_BIND = "bind";
const char *KEY_GROUP = "group";
const char *KEY_OUR_ID = "ourID";

const char *NO_FINAL_ACK = "\nDo not send final ack as fake transmission fault mode active\n";

const char *OPTIONS_SECTION = "options";
const char *SETTINGS_SECTION = "settings";

void initContext (Ctx& ctx) {
    ctx.chunkOffsets.clear ();
    ctx.chunkOffsets.push_back (0);
    ctx.lastChunkProcessed = -1;
    ctx.retransmissionExpected = false;
    ctx.addZerosAfter = -1;
    ctx.blockID = 0;
    ctx.lastSeqNo = 0;
    ctx.binaryFile = INVALID_HANDLE_VALUE;
}

SOCKET createSocket (Cfg& cfg) {
    SOCKET sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int trueVal = 1;

    setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, (char *) & trueVal, sizeof (trueVal));

    sockaddr_in bindData;

    bindData.sin_addr.S_un.S_addr = *cfg.bind ? inet_addr (cfg.bind) : htonl (INADDR_LOOPBACK);
    bindData.sin_port = htons (cfg.port);
    bindData.sin_family = AF_INET;

    if (bind (sock, (sockaddr *) & bindData, sizeof (bindData)) != 0) {
        closesocket (sock); sock = INVALID_SOCKET;
    } else {
        char hostName [100];
        ip_mreq request;
        
        gethostname (hostName, sizeof (hostName));

        auto hostEnt = gethostbyname (hostName);
    
        /*if (hostEnt) {
            // Subscribe all adapters to certain multicast group
            for (auto i = 0; hostEnt->h_addr_list [i]; ++ i) {
                request.imr_multiaddr.S_un.S_addr = cfg.group.S_un.S_addr;
                memcpy (& request.imr_interface.s_addr, hostEnt->h_addr_list [i], 4);
                setsockopt (sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) & request, sizeof (request));
            }
        } else*/ {
            // Subscribe everything what is possible
            request.imr_multiaddr.S_un.S_addr = inet_addr (cfg.group);
            request.imr_interface.S_un.S_addr = htonl (INADDR_LOOPBACK); //INADDR_ANY;
            setsockopt (sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) & request, sizeof (request));

            for (auto i = 0; hostEnt->h_addr_list [i]; ++ i) {
                request.imr_multiaddr.S_un.S_addr = inet_addr (cfg.group);
                memcpy (& request.imr_interface.s_addr, hostEnt->h_addr_list [i], 4);
                setsockopt (sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) & request, sizeof (request));
            }
        }
    }

    return sock;
}

void showMultiLines (char *firstPrefix, char *prefix, char *multiLines, size_t amount) {
    int count = 1;
    size_t sizePassed = 0;
    while (*multiLines && amount > 0) {
        printf ("%s%s\n", (count++) == 1 ? firstPrefix : prefix, multiLines);
        size_t size = strlen (multiLines) + 1;
        multiLines += size;
        amount -= size;
    }
}

void showVersionAndTokenType (Header *hdr) {
    printf (
        "Incoming binary transmission\n"
        "\tVersion:\t%d\n"
        "\tToken:\t\t%s\n",
        hdr->version,
        getTokenTypeName (getTokenType (hdr->token))
    );
}

void notifyLastChunkReceived (Ctx& ctx) {
    if (!ctx.retransmissionExpected) printf ("Last chunk received, send ack back to sender.\n");
}

void sendAck_v1 (Header1 *header, Ctx& ctx, char *title = 0, int chunkIndex = -1) {
    sockaddr_in dest;
    Header1 ackHeader;
    composeAck_v1 (header, & ackHeader, chunkIndex >= 0 ? chunkIndex : header->seqNum);

    dest.sin_addr.S_un.S_addr = inet_addr (ctx.config.group);
    dest.sin_port = htons (ctx.config.port);
    dest.sin_family = AF_INET;

    if (!title) {
        notifyLastChunkReceived (ctx);
    } else if (*title) {
        printf (title);
    }
    
    sendto (ctx.sock, (char *) & ackHeader, sizeof (ackHeader), 0, (sockaddr *) & dest, sizeof (dest));
}

void sendAck_v2 (Header2 *header, Ctx& ctx, char *title = 0, int chunkIndex = -1) {
    sockaddr_in dest;
    Header2 ackHeader;
    composeAck_v2 (header, & ackHeader, chunkIndex >= 0 ? chunkIndex + 1 : header->seqNum);

    dest.sin_addr.S_un.S_addr = inet_addr (ctx.config.group);
    dest.sin_port = htons (ctx.ackDestPort);
    dest.sin_family = AF_INET;

    if (!title) {
        notifyLastChunkReceived (ctx);
    } else if (*title) {
        printf (title);
    }

    sendto (ctx.sock, (char *) & ackHeader, sizeof (ackHeader), 0, (sockaddr *) & dest, sizeof (dest));
}

void createBinaryFile (Ctx& ctx, char *dataType) {
    if (ctx.binaryFile != INVALID_HANDLE_VALUE) CloseHandle (ctx.binaryFile);

    char path [MAX_PATH];
    char fileName [100];

    GetModuleFileNameA (0, path, sizeof (path));
    PathRemoveFileSpecA (path);
    PathAppendA (path, "save");
    PathAppendA (path, itoa (time (0), fileName, 10));

    if (strstr (dataType, "png")) {
        PathRenameExtensionA (path, ".png");
    } else if (strstr (dataType, "jpg")) {
        PathRenameExtensionA (path, ".jpg");
    } else if (strstr (dataType, "bmp")) {
        PathRenameExtensionA (path, ".bmp");
    } else {
        PathRenameExtensionA (path, ".dat");
    }

    ctx.binaryFile = CreateFile (path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

    printf ("Binary file %s is saving.\n", path);
}

void addChunkToBinaryFile (Ctx& ctx, int chunkIndex, char *data, size_t size) {
    unsigned long bytesWritten;

    SetFilePointer (ctx.binaryFile, ctx.chunkOffsets [chunkIndex], 0, SEEK_SET);
    WriteFile (ctx.binaryFile, data, size, & bytesWritten, 0);
}

void addZerosToBinaryFile (Ctx& ctx, int chunkIndex, size_t size) {
    char buffer [2000];
    unsigned long bytesWritten;

    memset (buffer, 0, size);
    SetFilePointer (ctx.binaryFile, ctx.chunkOffsets [chunkIndex], 0, SEEK_SET);
    WriteFile (ctx.binaryFile, buffer, size, & bytesWritten, 0);
}

void finalizeBinaryFile (Ctx& ctx) {
    if (ctx.binaryFile != INVALID_HANDLE_VALUE) {
        char path [MAX_PATH];

        printf ("\nFinalizing the binary file.\n");

        if (ctx.config.openImage) {
            GetFinalPathNameByHandle (ctx.binaryFile, path, sizeof (path), 0);
        }

        CloseHandle (ctx.binaryFile);

        if (ctx.config.openImage) {
            ShellExecute (GetConsoleWindow (), "open", path, 0, 0, SW_SHOW);
        }
    }
}

void notifyQueryReceived () {
    printf ("Query received from the sender\n");
}

void processQuery_v1 (Header1 *header, Ctx& ctx) {
    notifyQueryReceived ();
}

void processQuery_v2 (Header2 *header, Ctx& ctx) {
    notifyQueryReceived ();

    // No response to ack before all chunks have been received
    if (ctx.lastSeqNo != ctx.maxSeqNo) {
        printf ("Query ignored as not all chunks have been transferred.\n");
        return;
    }
}

void notifyAckReceived () {
    printf ("Ack received from the sender\n");
}

void processAck_v1 (Header1 *header, Ctx& ctx) {
    notifyAckReceived ();
}

void processAck_v2 (Header2 *header, Ctx& ctx) {
    notifyAckReceived ();
}

void notifyChunkReceived (uint32_t seqNum, uint32_t maxSeqNum, uint32_t amount) {
    printf ("Received chunk %d of %d (%d bytes)         \r", seqNum, maxSeqNum, amount);
}

void dumpHeader_v1 (Header1 *header) {
    addTextToDump ("\n*** Header ***\n");
    dump (0, (uint8_t *) header, sizeof (*header), false);
    addTextToDump ("\n*** Parsed details ***\n");
    addTextToDump ("Token\n");
    dump (0, (uint8_t *) header->token, sizeof (header->token), false);
    addTextToDump ("Version\n");
    dump (0, (uint8_t *) & header->version, sizeof (header->version), false);
    addTextToDump ("Source ID\n");
    dump (0, (uint8_t *) header->sourceID, sizeof (header->sourceID), false);
    addTextToDump ("Dest ID\n");
    dump (0, (uint8_t *) header->destID, sizeof (header->destID), false);
    addTextToDump ("Msg type\n");
    dump (0, (uint8_t *) & header->type, sizeof (header->type), false);
    addTextToDump ("Block ID\n");
    dump (0, (uint8_t *) & header->blockID, sizeof (header->blockID), false);
    addTextToDump ("Seq num\n");
    dump (0, (uint8_t *) & header->seqNum, sizeof (header->seqNum), false);
    addTextToDump ("Max seq num\n");
    dump (0, (uint8_t *) & header->maxSeqNum, sizeof (header->maxSeqNum), false);
}

void dumpHeader_v2 (Header2 *header) {
    addTextToDump ("\n*** Header ***\n");
    dump (0, (uint8_t *) header, sizeof (*header), false);
    addTextToDump ("\n*** Parsed details ***\n");
    addTextToDump ("Token\n");
    dump (0, (uint8_t *) header->token, sizeof (header->token), false);
    addTextToDump ("Version\n");
    dump (0, (uint8_t *) & header->version, sizeof (header->version), false);
    addTextToDump ("Header length\n");
    dump (0, (uint8_t *) & header->headerLength, sizeof (header->headerLength), false);
    addTextToDump ("Source ID\n");
    dump (0, (uint8_t *) header->sourceID, sizeof (header->sourceID), false);
    addTextToDump ("Dest ID\n");
    dump (0, (uint8_t *) header->destID, sizeof (header->destID), false);
    addTextToDump ("Msg type\n");
    dump (0, (uint8_t *) & header->type, sizeof (header->type), false);
    addTextToDump ("Block ID\n");
    dump (0, (uint8_t *) & header->blockID, sizeof (header->blockID), false);
    addTextToDump ("Seq num\n");
    dump (0, (uint8_t *) & header->seqNum, sizeof (header->seqNum), false);
    addTextToDump ("Max seq num\n");
    dump (0, (uint8_t *) & header->maxSeqNum, sizeof (header->maxSeqNum), false);
    addTextToDump ("Device\n");
    dump (0, (uint8_t *) & header->device, sizeof (header->device), false);
    addTextToDump ("Channel\n");
    dump (0, (uint8_t *) & header->channel, sizeof (header->channel), false);
}

void dumpData (char *data, size_t size) {
    char title [100];
    sprintf (title, "Data chunk (%zd bytes)\n", size);
    addTextToDump (title);
    dump (0, (uint8_t *) data, size, false);
}

void dumpFileDesc_v1 (FileDesc1 *desc) {
    size_t descLength = htonl (desc->length);
    size_t statusLength = descLength - (((uint8_t *) desc->textInfo - (uint8_t *) desc) + desc->typeLength);

    addTextToDump ("\n*** File descriptor ***\n");
    dump (0, (uint8_t *) desc, descLength, false);
    addTextToDump ("\n*** Parsed details ***\n");
    addTextToDump ("Length\n");
    dump (0, (uint8_t *) & desc->length, sizeof (desc->length), false);
    addTextToDump ("File length\n");
    dump (0, (uint8_t *) & desc->fileLength, sizeof (desc->fileLength), false);
    addTextToDump ("Status of ackquisition\n");
    dump (0, (uint8_t *) & desc->ackStatus, sizeof (desc->ackStatus), false);
    addTextToDump ("Device\n");
    dump (0, (uint8_t *) & desc->device, sizeof (desc->device), false);
    addTextToDump ("Channel\n");
    dump (0, (uint8_t *) & desc->channel, sizeof (desc->channel), false);
    addTextToDump ("Type length\n");
    dump (0, (uint8_t *) & desc->typeLength, sizeof (desc->typeLength), false);
    addTextToDump ("Data type\n");
    dump (0, (uint8_t *) desc->textInfo, desc->typeLength, false);
    addTextToDump ("Status and information text\n");
    dump (0, (uint8_t *) desc->textInfo + desc->typeLength, statusLength, false);
}

void dumpFileDesc_v2 (FileDesc2 *desc) {
    size_t descLength = htonl (desc->length);
    size_t statusLength = descLength - (((uint8_t *) desc->textInfo - (uint8_t *) desc) + desc->typeLength);

    addTextToDump ("\n*** File descriptor ***\n");
    dump (0, (uint8_t *) desc, descLength, false);
    addTextToDump ("\n*** Parsed details ***\n");
    addTextToDump ("Length\n");
    dump (0, (uint8_t *) & desc->length, sizeof (desc->length), false);
    addTextToDump ("File length\n");
    dump (0, (uint8_t *) & desc->fileLength, sizeof (desc->fileLength), false);
    addTextToDump ("Status of ackquisition\n");
    dump (0, (uint8_t *) & desc->ackStatus, sizeof (desc->ackStatus), false);
    addTextToDump ("Dest ack port\n");
    dump (0, (uint8_t *) & desc->ackDestPort, sizeof (desc->ackDestPort), false);
    addTextToDump ("Type length\n");
    dump (0, (uint8_t *) & desc->typeLength, sizeof (desc->typeLength), false);
    addTextToDump ("Data type\n");
    dump (0, (uint8_t *) desc->textInfo, desc->typeLength, false);
    addTextToDump ("Status length\n");
    dump (0, (uint8_t *) desc->textInfo + desc->typeLength, 2, false);
    addTextToDump ("Status and information text\n");
    dump (0, (uint8_t *) desc->textInfo + desc->typeLength + 2, statusLength, false);
}

void storeNextChunkOffset (Ctx& ctx, int curChunkIndex, size_t curChunkSize) {
    if (ctx.chunkOffsets.size () > curChunkIndex) {
        if (ctx.chunkOffsets.size () == (curChunkIndex + 1)) {
            ctx.chunkOffsets.push_back (ctx.chunkOffsets.back () + curChunkSize);
        } else {
            ctx.chunkOffsets [curChunkIndex+1] = ctx.chunkOffsets [curChunkIndex] + curChunkSize;
        }
    }
}

void parseChunk (char *stream, int amount, Ctx& ctx) {
    char buffer [2000];
    size_t size;
    Header *hdr = (Header *) buffer;
    uint8_t version = hdr->version;

    extractHeader ((uint8_t *) stream, buffer, & version, & size);

    char *binaryData;
    char ids [20], statusInfo [500], dataType [100];
    size_t dataTypeSize = sizeof (dataType), statusInfoSize = sizeof (statusInfo), fileDescSize, fileSize;
    uint8_t channel, device;
    uint16_t ackStatus, ackDestPort;
    int chunkIndex;
    bool lastChunk = false;

    if (version == 1) {
        Header1 *header = (Header1 *) buffer;

        // Dumping the header
        dumpHeader_v1 (header);

        // Ignore packets sent by ourselves
        if (memcmp (header->sourceID, ctx.config.ourID, 6) == 0) return;

        switch (header->type) {
            case MsgType::DATA: {
                // A normal data chunk received
                if (ctx.blockID == 0) ctx.blockID = header->blockID;
                
                if (header->blockID == ctx.blockID) {
                    ctx.lastSeqNo = header->seqNum;
                    chunkIndex = header->seqNum;
                } else {
                    // wrong block came, to be processed later on
                }
                break;
            }
            case MsgType::QUERY: {
                processQuery_v1 (header, ctx); return;
            }
            case MsgType::ACK: {
                processAck_v1 (header, ctx); return;
            }
        }

        notifyChunkReceived (header->seqNum, header->maxSeqNum, amount);

        if (header->seqNum == 0) {
            // First chunk found
            FileDesc1 *fileDesc = (FileDesc1 *)((uint8_t *) stream + sizeof (*header));

            // Dumping file descriptor
            dumpFileDesc_v1 (fileDesc);

            extractFileDescriptor_v1 (
                (uint8_t *) stream + sizeof (*header),
                & fileDescSize,
                & fileSize,
                & ackStatus,
                & device,
                & channel,
                dataType,
                & dataTypeSize,
                statusInfo,
                & statusInfoSize
            );

            binaryData = stream + sizeof (*header) + fileDescSize;

            // Dumping data
            dumpData (binaryData, amount - sizeof (*header) - fileDescSize);

            ctx.ackDestPort = 0;

            memset (ids, 0, sizeof (ids));
            memcpy (ids, header->sourceID, 6);
            memcpy (ids + 7, header->destID, 6);

            showVersionAndTokenType (hdr);

            printf (
                "\tsource:\t\t%s\n"
                "\tdest:\t\t%s\n"
                "\tsmg type:\t%s\n"
                "\tblock ID:\t%d\n"
                "\tdevice:\t\t%d\n"
                "\tchannel:\t%d\n"
                "\tack status:\t%d\n"
                "\tdata type:\t%s\n",
                ids,
                ids + 7,
                getTokenTypeName ((MsgTokenType) header->type),
                header->blockID,
                device,
                channel,
                ackStatus,
                dataType
            );
            createBinaryFile (ctx, dataType);
            showMultiLines ("\tStatus info:\n", "", statusInfo, statusInfoSize);
            //notifyChunkReceived (header->seqNum, header->maxSeqNum, amount);
        } else if (header->seqNum > (ctx.lastChunkProcessed + 1)) {
            // It seems that some chunks have been lost, send an error ack
            sendAck_v1 (header, ctx, "", ctx.lastChunkProcessed);
            
            ctx.retransmissionExpected = true; return;
        } else {
            if (header->seqNum <= ctx.lastChunkProcessed) {
                printf ("\nRetransmission attempt detected\n");

                if (ctx.config.stopWhenRetransmission) {
                    printf ("Exiting as the command line parameter -sr present.\n");
                    exit (0);
                }

                ctx.retransmissionExpected = false;
                ctx.addZerosAfter = -1;
            }

            if (header->seqNum == header->maxSeqNum) {
                lastChunk = true;

                notifyChunkReceived (header->seqNum, header->maxSeqNum, amount);

                if (ctx.retransmissionExpected) {
                    printf (NO_FINAL_ACK);
                } else if ((ctx.config.flags & ErrorFlags::SUPPRESS_FINAL_ACK) == 0) {
                    if (ctx.config.fakeAckAfterChunk < 0) {
                        sendAck_v1 (header, ctx);
                    } else {
                        printf (NO_FINAL_ACK);

                        // Suppress fake ack mode
                        ctx.config.fakeAckAfterChunk = -1;
                    }
                }
            }

            binaryData = stream + sizeof (*header);

            // Dumping data
            dumpData (binaryData, amount - sizeof (*header));
        }

        if (ctx.config.fakeAckAfterChunk == header->seqNum) {
            // Imitate a transmission error
            ctx.retransmissionExpected = true;
            
            // Force to write a wrong data
            ctx.addZerosAfter = chunkIndex;

            sendAck_v1 (header, ctx, "Imitate wrong reception\n", chunkIndex);

            // Decrease fault counter, suppress fake ack mode
            if ((--ctx.config.numberOfFakeAcks) == 0) {
                // do not force fake ack anymore, later we operate as usual
                ctx.config.fakeAckAfterChunk = -1;
            } else {
                // Next time we make a fake ack for the next packet
                ++ ctx.config.fakeAckAfterChunk;
            }
        }
    } else if (hdr->version == 2) {
        Header2 *header = (Header2 *) buffer;

        // Dumping the header
        dumpHeader_v2 (header);

        // Ignore packets sent by ourselves
        if (memcmp (header->sourceID, ctx.config.ourID, 6) == 0) return;

        switch (header->type) {
            case MsgType::DATA: {
                // A normal data chunk received
                if (ctx.blockID == 0) ctx.blockID = header->blockID;

                if (header->blockID == ctx.blockID) {
                    ctx.lastSeqNo = header->seqNum; 
                    chunkIndex = header->seqNum - 1;
                } else {
                    // wrong block came, to be processed later on
                }
                break;
            }
            case MsgType::QUERY: {
                processQuery_v2 (header, ctx); return;
            }
            case MsgType::ACK: {
                processAck_v2 (header, ctx); return;
            }
        }
            
        notifyChunkReceived (header->seqNum, header->maxSeqNum, amount);
        
        if (header->seqNum == 1) {
            // First chunk found
            FileDesc2 *fileDesc = (FileDesc2 *)((uint8_t *) stream + sizeof (*header));

            dumpFileDesc_v2 (fileDesc);

            extractFileDescriptor_v2 (
                (uint8_t *) stream + sizeof (*header),
                & fileDescSize,
                & fileSize,
                & ackStatus,
                & ackDestPort,
                dataType,
                & dataTypeSize,
                statusInfo,
                & statusInfoSize
            );

            binaryData = stream + sizeof (*header) + fileDescSize;

            // Dumping data
            dumpData (binaryData, amount - sizeof (*header) - fileDescSize);

            ctx.ackDestPort = ackDestPort;

            memset (ids, 0, sizeof (ids));
            memcpy (ids, header->sourceID, 6);
            memcpy (ids + 7, header->destID, 6);

            showVersionAndTokenType (hdr);

            printf (
                "\tsource:\t\t%s\n"
                "\tdest:\t\t%s\n"
                "\tsmg type:\t%s\n"
                "\tblock ID:\t%d\n"
                "\tdevice:\t\t%d\n"
                "\tchannel:\t%d\n"
                "\tack status:\t%d\n"
                "\tack dest port:\t%d\n"
                "\tdata type:\t%s\n",
                ids,
                ids + 7,
                getTokenTypeName ((MsgTokenType) header->type),
                header->blockID,
                header->device,
                header->channel,
                ackStatus,
                ackDestPort,
                dataType
            );
            createBinaryFile (ctx, dataType);
            showMultiLines ("\tStatus info:\n", "", statusInfo, statusInfoSize);
            //notifyChunkReceived (header->seqNum, header->maxSeqNum, amount);
        } else if (header->seqNum > (ctx.lastChunkProcessed + 2)) {
            // It seems that some chunks have been lost, send an error ack
            sendAck_v2 (header, ctx, "", ctx.lastChunkProcessed);
            
            ctx.retransmissionExpected = true; return;
        } else {
            if (header->seqNum <= ctx.lastChunkProcessed) {
                printf ("\nRetransmission attempt detected\n");

                if (ctx.config.stopWhenRetransmission) {
                    printf ("Exiting as the command line parameter -sr present.\n");
                    exit (0);
                }

                ctx.retransmissionExpected = false;
                ctx.addZerosAfter = -1;
            }

            if (header->seqNum == header->maxSeqNum) {
                lastChunk = true;
                
                notifyChunkReceived (header->seqNum, header->maxSeqNum, amount);

                if (ctx.retransmissionExpected) {
                    printf (NO_FINAL_ACK);
                } else if ((ctx.config.flags & ErrorFlags::SUPPRESS_FINAL_ACK) == 0) {
                    if (ctx.config.fakeAckAfterChunk < 0) {
                        sendAck_v2 (header, ctx);
                    } else {
                        printf (NO_FINAL_ACK);

                        // Suppress fake ack mode
                        ctx.config.fakeAckAfterChunk = -1;
                    }
                }
            }

            binaryData = stream + sizeof (*header);

            // Dumping data
            dumpData (binaryData, amount - sizeof (*header));
        }

        if (ctx.config.fakeAckAfterChunk == header->seqNum) {
            // Imitate a transmission error
            ctx.retransmissionExpected = true;

            // Force to write a wrong data
            ctx.addZerosAfter = chunkIndex;

            sendAck_v2 (header, ctx, "Imitate wrong reception\n", chunkIndex);

            // Decrease fault counter, suppress fake ack mode
            if ((--ctx.config.numberOfFakeAcks) == 0) {
                // do not force fake ack anymore, later we operate as usual
                ctx.config.fakeAckAfterChunk = -1;
            } else {
                // Next time we make a fake ack for the next packet
                ++ ctx.config.fakeAckAfterChunk;
            }
        }
    } else {
        printf ("Invalid version %d - ignoring\n", version); return;
    }

    auto dataSize = amount - (binaryData - stream);
    storeNextChunkOffset (ctx, chunkIndex, dataSize);

    if (ctx.addZerosAfter >= 0 && chunkIndex > ctx.addZerosAfter) {
        addZerosToBinaryFile (ctx, chunkIndex, dataSize);
    } else {
        addChunkToBinaryFile (ctx, chunkIndex, binaryData, dataSize);
    }

     ctx.lastChunkProcessed = chunkIndex;

    if (lastChunk && !ctx.retransmissionExpected) {
        finalizeBinaryFile (ctx);
        initContext (ctx);
    }
}

void showUsage () {
    printf (
        "bfrs [options]\n\n"
        "where options may be:\n"
        "\t-sr stops when retransmission detected (to demonstrate a corrupted file)\n"
        "\t-sfa supresses final ack send back to sender\n"
        "\t-fa:nnn fake acknowledge after chunk nnn\n"
    );
}

void onTimeoutExpired (Ctx& ctx) {
    printf ("\nTimeout expired, awating cancelled.\n");
    initContext (ctx);
}

int main (int argCount, char *args []) {
    Ctx ctx;
    WSADATA data;

    printf ("IEC 61162-450 Binary file receive simulator\n");
    
    WSAStartup (0x0202, & data);

    ctx.config.flags = 0;
    ctx.config.fakeAckAfterChunk = -1;
    ctx.blockID = 0;
    ctx.binaryFile = INVALID_HANDLE_VALUE;

    char cfgPath [MAX_PATH];
    GetModuleFileName (0, cfgPath, sizeof (cfgPath));
    PathRenameExtension (cfgPath, ".cfg");

    if (PathFileExists (cfgPath)) {
        ctx.config.fakeAckAfterChunk = GetPrivateProfileInt (OPTIONS_SECTION, "sendFakeAckAfterChunk", -1, cfgPath);
        ctx.config.numberOfFakeAcks = GetPrivateProfileInt (OPTIONS_SECTION, "numberOfFakeAcks", 1, cfgPath);
        ctx.config.stopWhenRetransmission = GetPrivateProfileInt (OPTIONS_SECTION, "stopWhenRetransmitting", 0, cfgPath) != 0;
        ctx.config.openImage = GetPrivateProfileInt (OPTIONS_SECTION, "openImage", 0, cfgPath) != 0;
        ctx.config.port = GetPrivateProfileInt (SETTINGS_SECTION, "port", 6026, cfgPath);
        ctx.config.timeout = GetPrivateProfileInt (SETTINGS_SECTION, "timeout", 1000, cfgPath);

        if (GetPrivateProfileInt (OPTIONS_SECTION, "suppressFinalAck", 0, cfgPath) != 0) {
            ctx.config.flags |= ErrorFlags::SUPPRESS_FINAL_ACK;
        }

        GetPrivateProfileString (SETTINGS_SECTION, "group", "239.192.0.26", ctx.config.group, sizeof (ctx.config.group), cfgPath);
        GetPrivateProfileString (SETTINGS_SECTION, "bind", "", ctx.config.bind, sizeof (ctx.config.bind), cfgPath);
        GetPrivateProfileString (SETTINGS_SECTION, "ourID", "VR0001", ctx.config.ourID, sizeof (ctx.config.ourID), cfgPath);
    } else {
        for (int i = 1; i < argCount; ++ i) {
            char *arg = args [i];

            if (stricmp (arg, "-?") == 0 || stricmp (arg, "-h") == 0) {
                showUsage ();
                exit (0);
            }

            if (stricmp (arg, "-sr") == 0) {
                ctx.config.stopWhenRetransmission = true;
            }

            if (stricmp (arg, "-sfa") == 0) {
                ctx.config.flags |= ErrorFlags::SUPPRESS_FINAL_ACK;
            }

            if (strnicmp (arg, "-fa:", 4) == 0) {
                ctx.config.fakeAckAfterChunk = atoi (arg + 4);
            }
        }
    }

    ctx.sock = createSocket (ctx.config);
    ctx.binaryFile = INVALID_HANDLE_VALUE;

    initContext (ctx);

    char buffer [2000];
    unsigned long bytesAvailable;
    int senderSize;

    printf ("Waiting for data...\n");

    char logPath [MAX_PATH];
    GetModuleFileName (0, logPath, sizeof (logPath));
    PathRenameExtension (logPath, ".dmp");

    time_t lastReceiption = 0;

    while (true) {
        time_t now = time (0);

        if ((lastReceiption > 0) && ((now - lastReceiption) > ctx.config.timeout)) {
            lastReceiption = 0;
            onTimeoutExpired (ctx);
        } else {
            bytesAvailable = 0;
            int result = ioctlsocket (ctx.sock, FIONREAD, & bytesAvailable);
            if (result == 0 && bytesAvailable > 0) {
                sockaddr_in sender;
                senderSize = sizeof (sender);

                lastReceiption = now;

                auto bytesRead = recvfrom (ctx.sock, buffer, bytesAvailable, 0, (sockaddr *) & sender, & senderSize);

                dump (logPath, (uint8_t *) buffer, bytesRead);
                parseChunk (buffer, bytesRead, ctx);
            }
        }

        Sleep ((DWORD) 5);
    }

    closesocket (ctx.sock);
    exit (0);
}
