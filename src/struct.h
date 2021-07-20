#ifndef STRUCT_H_INCLUDED
#define STRUCT_H_INCLUDED

#pragma pack(push)
#pragma pack(1)

struct IPCMemoryInfo
{
    uint8_t airIoStatus[6];
    uint8_t sliderIoStatus[32];
    uint8_t ledRgbData[32 * 3];
    uint8_t testBtn;
    uint8_t serviceBtn;
    uint8_t coinInsertion;
    uint8_t cardRead;
    uint8_t remoteCardRead;
    uint8_t remoteCardType;
    uint8_t remoteCardId[10];
};

struct PacketInput
{
    uint8_t packetSize;
    uint8_t packetName[3];
    uint32_t packetId;
    uint8_t airIoStatus[6];
    uint8_t sliderIoStatus[32];
    uint8_t testBtn;
    uint8_t serviceBtn;
};

struct PacketInputNoAir
{
    uint8_t packetSize;
    uint8_t packetName[3];
    uint32_t packetId;
    uint8_t sliderIoStatus[32];
    uint8_t testBtn;
    uint8_t serviceBtn;
};

struct PacketFunction
{
    uint8_t packetSize;
    uint8_t packetName[3];
    uint8_t funcBtn;
};

struct PacketConnect
{
    uint8_t packetSize;
    uint8_t packetName[3];
    uint8_t addrType;
    uint16_t port;
    union
    {
        struct
        {
            uint8_t addr[4];
            uint8_t padding[12];
        } addr4;
        uint8_t addr6[16];
    } addr;
};

struct PacketCard
{
    uint8_t packetSize;
    uint8_t packetName[3];
    uint8_t remoteCardRead;
    uint8_t remoteCardType;
    uint8_t remoteCardId[10];
};

struct PacketPing
{
    uint8_t packetSize;
    uint8_t packetName[3];
    uint64_t remotePingTime;
};

#pragma pack(pop)

#endif // STRUCT_H_INCLUDED
