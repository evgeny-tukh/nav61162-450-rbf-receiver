#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>
#include <WinSock.h>
#include <Shlwapi.h>
#include <stdint.h>
#include <time.h>

#include "61162_450_defs.h"
#include "61162_450_interface.h"

enum ErrorFlags {
    SUPPRESS_FINAL_ACK = 1,
};

struct Cfg {
    in_addr bind;
    in_addr group;
    uint16_t port;
    uint32_t flags;
    int fakeAckAfterChunk;
    char ourID [7];
    bool openFile, cleanupLog;
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
};

const char *SECTION_CFG = "cfg";
const char *KEY_PORT = "port";
const char *KEY_BIND = "bind";
const char *KEY_GROUP = "group";
const char *KEY_OUR_ID = "ourID";

const char *NO_FINAL_ACK = "Do not send final ack as fake transmission fault mode active\n";

void loadCfg (Cfg& cfg) {
    char path[MAX_PATH];

    auto getCfgInt = [path] (const char *key, int defValue) {
        return GetPrivateProfileIntA (SECTION_CFG, key, defValue, path);
    };
    auto getCfgAddr = [path] (const char *key, char *defValue = "") {
        char buffer [50];

        GetPrivateProfileStringA (SECTION_CFG, key, defValue, buffer, sizeof (buffer), path);
        
        return *buffer ? inet_addr (buffer) : INADDR_ANY;
    };

    memset (& cfg, 0, sizeof (cfg));

    GetModuleFileNameA (0, path, sizeof (path));
    PathRemoveFileSpecA (path);
    PathAppendA (path, "config.dat");

    cfg.port = getCfgInt (KEY_PORT, 60026);
    cfg.bind.S_un.S_addr = getCfgAddr (KEY_BIND);
    cfg.group.S_un.S_addr = getCfgAddr (KEY_BIND, "239.192.0.26");

    GetPrivateProfileStringA (SECTION_CFG, KEY_OUR_ID, "VR0001", cfg.ourID, sizeof (cfg.ourID), path);
}

SOCKET createSocket (Cfg& cfg) {
    SOCKET sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int trueVal = 1;

    setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, (char *) & trueVal, sizeof (trueVal));

    sockaddr_in bindData;

    bindData.sin_addr.S_un.S_addr = cfg.bind.S_un.S_addr;
    bindData.sin_port = htons (cfg.port);
    bindData.sin_family = AF_INET;

    if (bind (sock, (sockaddr *) & bindData, sizeof (bindData)) != 0) {
        closesocket (sock); sock = INVALID_SOCKET;
    } else {
        char hostName [100];
        ip_mreq request;
        int bufSize = 20480000;
        
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) & bufSize, sizeof (bufSize));
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
            request.imr_multiaddr.S_un.S_addr = cfg.group.S_un.S_addr;
            request.imr_interface.S_un.S_addr = htonl (INADDR_LOOPBACK); //INADDR_ANY;
            setsockopt (sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) & request, sizeof (request));

            for (auto i = 0; hostEnt->h_addr_list [i]; ++ i) {
                request.imr_multiaddr.S_un.S_addr = cfg.group.S_un.S_addr;
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

void showVersionAndTokenType (Nav61162_450::Header *hdr) {
    printf (
        "Incoming binary transmission\n"
        "\tVersion:\t%d\n"
        "\tToken:\t\t%s\n",
        hdr->version,
        Nav61162_450::getTokenTypeName (Nav61162_450::getTokenType (hdr->token))
    );
}

void sendAck_v1 (Nav61162_450::Header1 *header, Ctx& ctx, char *title = 0) {
    sockaddr_in dest;
    Nav61162_450::Header1 ackHeader;
    Nav61162_450::composeFinalAck_v1 (header, & ackHeader);

    dest.sin_addr.S_un.S_addr = ctx.config.group.S_un.S_addr;
    dest.sin_port = htons (ctx.config.port);
    dest.sin_family = AF_INET;

    printf (title ? title : "Last chunk received, send ack back to sender.\n");
    sendto (ctx.sock, (char *) & ackHeader, sizeof (ackHeader), 0, (sockaddr *) & dest, sizeof (dest));
}

void sendAck_v2 (Nav61162_450::Header2 *header, Ctx& ctx, char *title = 0) {
    sockaddr_in dest;
    Nav61162_450::Header2 ackHeader;
    Nav61162_450::composeFinalAck_v2 (header, & ackHeader);

    dest.sin_addr.S_un.S_addr = ctx.config.group.S_un.S_addr;
    dest.sin_port = htons (ctx.ackDestPort);
    dest.sin_family = AF_INET;

    printf (title ? title : "Last chunk received, send ack back to sender.\n");
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

void addChunkToBinaryFile (Ctx& ctx, char *data, size_t size) {
    unsigned long bytesWritten;

    WriteFile (ctx.binaryFile, data, size, & bytesWritten, 0);
}

void finalizeBinaryFile (Ctx& ctx) {
    if (ctx.binaryFile != INVALID_HANDLE_VALUE) {
        char path [MAX_PATH];
        GetFinalPathNameByHandle (ctx.binaryFile, path, sizeof (path), 0);
        
        printf ("\nFinalizing the binary file.\n");
        CloseHandle (ctx.binaryFile);

        if (ctx.config.openFile) {
            ShellExecute (GetConsoleWindow (), "open", path, 0, 0, SW_SHOW);
        }
    }
}

void notifyQueryReceived () {
    printf ("\nQuery received from the sender\n");
}

void processQuery_v1 (Nav61162_450::Header1 *header, Ctx& ctx) {
    notifyQueryReceived ();
}

void processQuery_v2 (Nav61162_450::Header2 *header, Ctx& ctx) {
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

void processAck_v1 (Nav61162_450::Header1 *header, Ctx& ctx) {
    notifyAckReceived ();
}

void processAck_v2 (Nav61162_450::Header2 *header, Ctx& ctx) {
    notifyAckReceived ();
}

void notifyChunkReceived (uint32_t seqNum, uint32_t maxSeqNum, uint32_t amount) {
    printf ("Received chunk %d of %d (%d bytes)         \r", seqNum, maxSeqNum, amount);
}

void dumpHeader_v1 (Nav61162_450::Header1 *header) {
    Nav61162_450::addTextToDump ("\n*** Header ***\n");
    Nav61162_450::dump (0, (uint8_t *) header, sizeof (*header), false);
    Nav61162_450::addTextToDump ("\n*** Parsed details ***\n");
    Nav61162_450::addTextToDump ("Token\n");
    Nav61162_450::dump (0, (uint8_t *) header->token, sizeof (header->token), false);
    Nav61162_450::addTextToDump ("Version\n");
    Nav61162_450::dump (0, (uint8_t *) & header->version, sizeof (header->version), false);
    Nav61162_450::addTextToDump ("Source ID\n");
    Nav61162_450::dump (0, (uint8_t *) header->sourceID, sizeof (header->sourceID), false);
    Nav61162_450::addTextToDump ("Dest ID\n");
    Nav61162_450::dump (0, (uint8_t *) header->destID, sizeof (header->destID), false);
    Nav61162_450::addTextToDump ("Msg type\n");
    Nav61162_450::dump (0, (uint8_t *) & header->type, sizeof (header->type), false);
    Nav61162_450::addTextToDump ("Block ID\n");
    Nav61162_450::dump (0, (uint8_t *) & header->blockID, sizeof (header->blockID), false);
    Nav61162_450::addTextToDump ("Seq num\n");
    Nav61162_450::dump (0, (uint8_t *) & header->seqNum, sizeof (header->seqNum), false);
    Nav61162_450::addTextToDump ("Max seq num\n");
    Nav61162_450::dump (0, (uint8_t *) & header->maxSeqNum, sizeof (header->maxSeqNum), false);
}

void dumpHeader_v2 (Nav61162_450::Header2 *header) {
    Nav61162_450::addTextToDump ("\n*** Header ***\n");
    Nav61162_450::dump (0, (uint8_t *) header, sizeof (*header), false);
    Nav61162_450::addTextToDump ("\n*** Parsed details ***\n");
    Nav61162_450::addTextToDump ("Token\n");
    Nav61162_450::dump (0, (uint8_t *) header->token, sizeof (header->token), false);
    Nav61162_450::addTextToDump ("Version\n");
    Nav61162_450::dump (0, (uint8_t *) & header->version, sizeof (header->version), false);
    Nav61162_450::addTextToDump ("Header length\n");
    Nav61162_450::dump (0, (uint8_t *) & header->headerLength, sizeof (header->headerLength), false);
    Nav61162_450::addTextToDump ("Source ID\n");
    Nav61162_450::dump (0, (uint8_t *) header->sourceID, sizeof (header->sourceID), false);
    Nav61162_450::addTextToDump ("Dest ID\n");
    Nav61162_450::dump (0, (uint8_t *) header->destID, sizeof (header->destID), false);
    Nav61162_450::addTextToDump ("Msg type\n");
    Nav61162_450::dump (0, (uint8_t *) & header->type, sizeof (header->type), false);
    Nav61162_450::addTextToDump ("Block ID\n");
    Nav61162_450::dump (0, (uint8_t *) & header->blockID, sizeof (header->blockID), false);
    Nav61162_450::addTextToDump ("Seq num\n");
    Nav61162_450::dump (0, (uint8_t *) & header->seqNum, sizeof (header->seqNum), false);
    Nav61162_450::addTextToDump ("Max seq num\n");
    Nav61162_450::dump (0, (uint8_t *) & header->maxSeqNum, sizeof (header->maxSeqNum), false);
    Nav61162_450::addTextToDump ("Device\n");
    Nav61162_450::dump (0, (uint8_t *) & header->device, sizeof (header->device), false);
    Nav61162_450::addTextToDump ("Channel\n");
    Nav61162_450::dump (0, (uint8_t *) & header->channel, sizeof (header->channel), false);
}

void dumpData (char *data, size_t size) {
    char title [100];
    sprintf (title, "Data chunk (%zd bytes)\n", size);
    Nav61162_450::addTextToDump (title);
    Nav61162_450::dump (0, (uint8_t *) data, size, false);
}

void dumpFileDesc_v1 (Nav61162_450::FileDesc1 *desc) {
    size_t descLength = htonl (desc->length);
    size_t statusLength = descLength - (((uint8_t *) desc->textInfo - (uint8_t *) desc) + desc->typeLength);

    Nav61162_450::addTextToDump ("\n*** File descriptor ***\n");
    Nav61162_450::dump (0, (uint8_t *) desc, descLength, false);
    Nav61162_450::addTextToDump ("\n*** Parsed details ***\n");
    Nav61162_450::addTextToDump ("Length\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->length, sizeof (desc->length), false);
    Nav61162_450::addTextToDump ("File length\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->fileLength, sizeof (desc->fileLength), false);
    Nav61162_450::addTextToDump ("Status of ackquisition\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->ackStatus, sizeof (desc->ackStatus), false);
    Nav61162_450::addTextToDump ("Device\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->device, sizeof (desc->device), false);
    Nav61162_450::addTextToDump ("Channel\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->channel, sizeof (desc->channel), false);
    Nav61162_450::addTextToDump ("Type length\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->typeLength, sizeof (desc->typeLength), false);
    Nav61162_450::addTextToDump ("Data type\n");
    Nav61162_450::dump (0, (uint8_t *) desc->textInfo, desc->typeLength, false);
    Nav61162_450::addTextToDump ("Status and information text\n");
    Nav61162_450::dump (0, (uint8_t *) desc->textInfo + desc->typeLength, statusLength, false);
}

void dumpFileDesc_v2 (Nav61162_450::FileDesc2 *desc) {
    size_t descLength = htonl (desc->length);
    size_t statusLength = descLength - (((uint8_t *) desc->textInfo - (uint8_t *) desc) + desc->typeLength);

    Nav61162_450::addTextToDump ("\n*** File descriptor ***\n");
    Nav61162_450::dump (0, (uint8_t *) desc, descLength, false);
    Nav61162_450::addTextToDump ("\n*** Parsed details ***\n");
    Nav61162_450::addTextToDump ("Length\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->length, sizeof (desc->length), false);
    Nav61162_450::addTextToDump ("File length\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->fileLength, sizeof (desc->fileLength), false);
    Nav61162_450::addTextToDump ("Status of ackquisition\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->ackStatus, sizeof (desc->ackStatus), false);
    Nav61162_450::addTextToDump ("Dest ack port\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->ackDestPort, sizeof (desc->ackDestPort), false);
    Nav61162_450::addTextToDump ("Type length\n");
    Nav61162_450::dump (0, (uint8_t *) & desc->typeLength, sizeof (desc->typeLength), false);
    Nav61162_450::addTextToDump ("Data type\n");
    Nav61162_450::dump (0, (uint8_t *) desc->textInfo, desc->typeLength, false);
    Nav61162_450::addTextToDump ("Status length\n");
    Nav61162_450::dump (0, (uint8_t *) desc->textInfo + desc->typeLength, 2, false);
    Nav61162_450::addTextToDump ("Status and information text\n");
    Nav61162_450::dump (0, (uint8_t *) desc->textInfo + desc->typeLength + 2, statusLength, false);
}

void parseChunk (char *stream, int amount, Ctx& ctx) {
    char buffer [2000];
    size_t size;
    Nav61162_450::Header *hdr = (Nav61162_450::Header *) buffer;
    uint8_t version = hdr->version;

    Nav61162_450::extractHeader ((uint8_t *) stream, buffer, & version, & size);

    char *binaryData;
    char ids [20], statusInfo [500], dataType [100];
    size_t dataTypeSize = sizeof (dataType), statusInfoSize = sizeof (statusInfo), fileDescSize, fileSize;
    uint8_t channel, device;
    uint16_t ackStatus, ackDestPort;
    bool lastChunk = false;

    if (version == 1) {
        Nav61162_450::Header1 *header = (Nav61162_450::Header1 *) buffer;

        // Dumping the header
        dumpHeader_v1 (header);

        // Ignore packets sent by ourselves
        if (memcmp (header->sourceID, ctx.config.ourID, 6) == 0) return;

        switch (header->type) {
            case Nav61162_450::MsgType::DATA: {
                // A normal data chunk received
                if (header->blockID == ctx.blockID) {
                    ctx.lastSeqNo = header->seqNum; 
                }
                break;
            }
            case Nav61162_450::MsgType::QUERY: {
                processQuery_v1 (header, ctx); return;
            }
            case Nav61162_450::MsgType::ACK: {
                processAck_v1 (header, ctx); return;
            }
        }

        if (header->seqNum == 0) {
            // First chunk found
            Nav61162_450::FileDesc1 *fileDesc = (Nav61162_450::FileDesc1 *)((uint8_t *) stream + sizeof (*header));

            // Dumping file descriptor
            dumpFileDesc_v1 (fileDesc);

            Nav61162_450::extractFileDescriptor_v1 (
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
                Nav61162_450::getTokenTypeName ((Nav61162_450::TokenType) header->type),
                header->blockID,
                device,
                channel,
                ackStatus,
                dataType
            );
            createBinaryFile (ctx, dataType);
            showMultiLines ("\tStatus info:\n", "", statusInfo, statusInfoSize);
            notifyChunkReceived (header->seqNum, header->maxSeqNum, amount);
        } else {
            if (header->seqNum == header->maxSeqNum) {
                lastChunk = true;

                notifyChunkReceived (header->seqNum, header->maxSeqNum, amount);

                if ((ctx.config.flags & ErrorFlags::SUPPRESS_FINAL_ACK) == 0) {
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
            sendAck_v1 (header, ctx, "Imitate wrong reception\n");
        }
    } else if (hdr->version == 2) {
        Nav61162_450::Header2 *header = (Nav61162_450::Header2 *) buffer;

        // Dumping the header
        dumpHeader_v2 (header);

        // Ignore packets sent by ourselves
        if (memcmp (header->sourceID, ctx.config.ourID, 6) == 0) return;

        switch (header->type) {
            case Nav61162_450::MsgType::DATA: {
                // A normal data chunk received
                if (header->blockID == ctx.blockID) {
                    ctx.lastSeqNo = header->seqNum; 
                }
                break;
            }
            case Nav61162_450::MsgType::QUERY: {
                processQuery_v2 (header, ctx); return;
            }
            case Nav61162_450::MsgType::ACK: {
                processAck_v2 (header, ctx); return;
            }
        }
            
        if (header->seqNum == 1) {
            // First chunk found
            Nav61162_450::FileDesc2 *fileDesc = (Nav61162_450::FileDesc2 *)((uint8_t *) stream + sizeof (*header));

            dumpFileDesc_v2 (fileDesc);

            Nav61162_450::extractFileDescriptor_v2 (
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
                Nav61162_450::getTokenTypeName ((Nav61162_450::TokenType) header->type),
                header->blockID,
                header->device,
                header->channel,
                ackStatus,
                ackDestPort,
                dataType
            );
            createBinaryFile (ctx, dataType);
            showMultiLines ("\tStatus info:\n", "", statusInfo, statusInfoSize);
            notifyChunkReceived (header->seqNum, header->maxSeqNum, amount);
        } else {
            if (header->seqNum == header->maxSeqNum) {
                lastChunk = true;
                
                notifyChunkReceived (header->seqNum, header->maxSeqNum, amount);

                if ((ctx.config.flags & ErrorFlags::SUPPRESS_FINAL_ACK) == 0) {
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
            sendAck_v2 (header, ctx, "Imitate wrong reception\n");
        }
    } else {
        printf ("Invalid version %d - ignoring\n", version); return;
    }

    addChunkToBinaryFile (ctx, binaryData, amount - (binaryData - stream));

    if (lastChunk) {
        finalizeBinaryFile (ctx);

        ctx.lastSeqNo = 0;
    }
}

void showUsage () {
    printf (
        "bfrs [options]\n\n"
        "where options may be:\n"
        "\t-c\tcleans the dump file up at start\n"
        "\t-o\topens the received file after sucessful receiption\n"
        "\t-sfa\tsupresses final ack send back to sender\n"
        "\t-fa:nnn\tfake acknowledge after chunk nnn\n"
    );
}

int main (int argCount, char *args []) {
    Ctx ctx;
    WSADATA data;

    printf ("IEC 61162-450 Binary file receive simulator\n");
    
    WSAStartup (0x0202, & data);

    loadCfg (ctx.config);

    ctx.config.flags = 0;
    ctx.config.fakeAckAfterChunk = -1;
    ctx.config.openFile = false;

    for (int i = 1; i < argCount; ++ i) {
        char *arg = args [i];

        if (stricmp (arg, "-?") == 0 || stricmp (arg, "-h") == 0) {
            showUsage ();
            exit (0);
        }

        if (stricmp (arg, "-sfa") == 0) {
            ctx.config.flags |= ErrorFlags::SUPPRESS_FINAL_ACK;
        }
        if (strnicmp (arg, "-fa:", 4) == 0) {
            ctx.config.fakeAckAfterChunk = atoi (arg + 4);
        }
        if (strnicmp (arg, "-o", 2) == 0) {
            ctx.config.openFile = true;
        }
        if (strnicmp (arg, "-c", 2) == 0) {
            ctx.config.cleanupLog = true;
        }
    }

    ctx.sock = createSocket (ctx.config);
    ctx.binaryFile = INVALID_HANDLE_VALUE;

    char buffer [2000];
    unsigned long bytesAvailable;
    int senderSize;

    char logPath [MAX_PATH];
    GetModuleFileName (0, logPath, sizeof (logPath));
    PathRenameExtension (logPath, ".dmp");

    if (ctx.config.cleanupLog) {
        printf ("Wiping log off...\n");
        Nav61162_450::cleanupLog (logPath);
    }

    printf ("Waiting for data...\n");

    while (true) {
        bytesAvailable = 0;
        int result = ioctlsocket (ctx.sock, FIONREAD, & bytesAvailable);
        if (result == 0 && bytesAvailable > 0) {
            sockaddr_in sender;
            senderSize = sizeof (sender);

            auto bytesRead = recvfrom (ctx.sock, buffer, bytesAvailable, 0, (sockaddr *) & sender, & senderSize);

            if (bytesRead > 0) {
                Nav61162_450::dump (logPath, (uint8_t *) buffer, bytesRead);
                parseChunk (buffer, bytesRead, ctx);
            }
        }
        Sleep ((DWORD) 0);
    }

    closesocket (ctx.sock);
    exit (0);
}
