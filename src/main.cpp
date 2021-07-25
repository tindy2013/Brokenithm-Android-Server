#include <string>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>

#include "socket.h"
#include "defer.h"
#include "version.h"
#include "struct.h"
#include <windows.h>

std::string remote_address;
uint16_t remote_port = 52468;
uint16_t server_port = 52468;
bool tcp_mode = false;

size_t tcp_buffer_size = 96;
size_t tcp_receive_threshold = 48;

std::atomic_bool EXIT_FLAG {false}, CONNECTED {false};

void socketSetTimeout(SOCKET sHost, int timeout)
{
    setsockopt(sHost, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(int));
    setsockopt(sHost, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(int));
}

int socketBind(SOCKET sHost, long addr, uint16_t port)
{
    sockaddr_in srcaddr = {};
    memset(&srcaddr, 0, sizeof(srcaddr));
    srcaddr.sin_family = AF_INET;
    srcaddr.sin_addr.s_addr = addr;
    srcaddr.sin_port = htons(port);
    return bind(sHost, reinterpret_cast<sockaddr*>(&srcaddr), sizeof(srcaddr));
}

sockaddr_in makeBroadcastAddr(uint16_t port)
{
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    addr.sin_port = htons(port);
    return addr;
}

sockaddr_in makeIPv4Addr(const std::string &host, uint16_t port)
{
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, host.data(), (struct in_addr *)&addr.sin_addr.s_addr);
    addr.sin_port = htons(port);
    return addr;
}

int socketSendTo(SOCKET sHost, const sockaddr_in &addr, const std::string &data)
{
    return sendto(sHost, data.data(), data.size(), 0, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
}

std::string getTime(int type)
{
    time_t lt;
    char tmpbuf[32], cMillis[7];
    std::string format;
    timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(cMillis, 7, "%.6ld", (long)tv.tv_usec);
    lt = time(NULL);
    struct tm *local = localtime(&lt);
    switch(type)
    {
    case 1:
        format = "%Y%m%d-%H%M%S";
        break;
    case 2:
        format = "%Y/%m/%d %a %H:%M:%S." + std::string(cMillis);
        break;
    case 3:
        format = "%Y-%m-%d %H:%M:%S";
        break;
    }
    strftime(tmpbuf, 32, format.data(), local);
    return std::string(tmpbuf);
}

template <typename... Args>
void printErr(const char* format, Args... args)
{
    std::string time = "[" + getTime(2) + "] ";
    fprintf(stderr, time.data());
    fprintf(stderr, format, args...);
}

void threadLEDBroadcast(SOCKET sHost, const IPCMemoryInfo* memory)
{
    static std::string previous_status;
    static int skip_count = 0;
    static std::string head = "\x63LED";
    auto addr = makeIPv4Addr(remote_address, remote_port);
    while(!EXIT_FLAG)
    {
        if(!CONNECTED) {
            Sleep(50);
            continue;
        }
        std::string current_status;
        current_status.assign(reinterpret_cast<const char*>(memory->ledRgbData), sizeof(memory->ledRgbData));
        bool same = true;
        if(!previous_status.empty())
        {
            if(memcmp(previous_status.data(), current_status.data(), previous_status.size()) != 0)
            {
                same = false;
                break;
            }
        }
        else
            same = false;
        previous_status = current_status;
        if(!same)
        {
            current_status.insert(0, head);
            if(socketSendTo(sHost, addr, current_status) < 0)
            {
                printErr("[Error] Cannot send packet: error %lu\n", GetLastError());
                if(tcp_mode)
                {
                    if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                    {
                        continue;
                    }
                    else
                    {
                        printErr("[INFO] Device disconnected!\n");
                        CONNECTED = false;
                        EXIT_FLAG = true;
                        break;
                    }
                }
            }
            skip_count = 0;
        }
        else
        {
            if(++skip_count > 50)
            {
                current_status.insert(0, head);
                if(socketSendTo(sHost, addr, current_status) < 0)
                {
                    printErr("[ERROR] Cannot send packet: error %lu\n", GetLastError());
                    if(tcp_mode)
                    {
                        if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                        {
                            continue;
                        }
                        else
                        {
                            printErr("[INFO] Device disconnected!\n");
                            CONNECTED = false;
                            EXIT_FLAG = true;
                            break;
                        }
                    }
                }
                skip_count = 0;
            }
        }
        Sleep(10);
    }
}

enum
{
    FUNCTION_COIN = 1,
    FUNCTION_CARD
};

void getSocksAddress(const PacketConnect* pkt, std::string &address, uint16_t &port)
{
    char cAddr[128] = {};
    std::string retAddr;
    int family = pkt->addrType;
    port = ntohs(pkt->port);
    switch(family)
    {
    case 1: //IPv4
        inet_ntop(AF_INET, pkt->addr.addr4.addr, cAddr, 127);
        break;
    case 2: //IPv6
        inet_ntop(AF_INET6, pkt->addr.addr6, cAddr, 127);
        break;
    }
    address.assign(cAddr);
}

uint32_t last_input_packet_id = 0;

void updatePacketId(uint32_t newPacketId)
{
    if(last_input_packet_id > newPacketId)
    {
        printErr("[WARN] Packet #%" PRIu32 " came too late\n", newPacketId);
    }
    else if(newPacketId > last_input_packet_id + 1)
    {
        printErr("[WARN] Packets between #%" PRIu32 " and #%" PRIu32 " total %" PRIu32 " packet(s) are missing, probably too late or dropped\n", last_input_packet_id, newPacketId, newPacketId - last_input_packet_id - 1);
    }
    else if(newPacketId == last_input_packet_id)
    {
        printErr("[WARN] Packet #%" PRIu32 " duplicated\n", newPacketId);
    }
    last_input_packet_id = newPacketId;
}

template <typename... Args>
void dprintf(const char* format, Args... args)
{
    fprintf(stderr, format, args...);
}

void dump(const void *ptr, size_t nbytes, bool hex_string = false)
{
    const uint8_t *bytes;
    uint8_t c;
    size_t i;
    size_t j;

    if (nbytes == 0) {
        dprintf("\t--- Empty ---\n");
    }

    bytes = (const unsigned char*)ptr;

    if (hex_string) {
        for (i = 0 ; i < nbytes ; i++) {
            dprintf("%02x", bytes[i]);
        }
        dprintf("\n");
        return;
    }

    for (i = 0 ; i < nbytes ; i += 16) {
        dprintf("    %08x:", (int) i);

        for (j = 0 ; i + j < nbytes && j < 16 ; j++) {
            dprintf(" %02x", bytes[i + j]);
        }

        while (j < 16) {
            dprintf("   ");
            j++;
        }

        dprintf(" ");

        for (j = 0 ; i + j < nbytes && j < 16 ; j++) {
            c = bytes[i + j];

            if (c < 0x20 || c >= 0x7F) {
                c = '.';
            }

            dprintf("%c", c);
        }

        dprintf("\n");
    }

    dprintf("\n");
}

enum
{
    CARD_AIME,
    CARD_FELICA
};

void printCardInfo(uint8_t cardType, uint8_t *cardId)
{
    switch(cardType)
    {
    case CARD_AIME:
        printErr("[INFO] Card Type: Aime\t\tID: ");
        dump(cardId, 10, true);
        break;
    case CARD_FELICA:
        printErr("[INFO] Card Type: FeliCa\tIDm: ");
        dump(cardId, 8, true);
        break;
    }
}

void threadInputReceive(SOCKET sHost, IPCMemoryInfo *memory)
{
    char recv_buffer[tcp_buffer_size];
    char buffer[BUFSIZ];
    std::string remains;
    auto addr = makeIPv4Addr(remote_address, remote_port);
    while(!EXIT_FLAG)
    {
        int recv_len, real_len;
        size_t packet_len;
        uint32_t current_packet_id;
        if(!tcp_mode)
        {
            /**
                on UDP mode data is sent as packets, so just receive into a buffer big enough for 1 packet
                each recvfrom call will only get 1 packet of data, the remaining data is discarded
            **/

            if((recv_len = recvfrom(sHost, buffer, BUFSIZ - 1, 0, NULL, NULL)) == -1)
                continue;
            real_len = buffer[0];
            if(real_len > recv_len)
                continue;
            packet_len = real_len + 1;
        }
        else
        {
            /**
                on TCP mode data is sent as stream, one recvfrom call may receive multiple packets
                so we need to store the remaining data when real_len > recv_len
            **/
            if(remains.size() < tcp_receive_threshold)
            {
                if((recv_len = recv(sHost, recv_buffer, tcp_buffer_size - 1, 0)) == -1)
                    continue;
                remains.append(recv_buffer, recv_len);
            }

            int data_left = remains.size();
            real_len = remains[0];
            if(real_len > data_left)
                continue;
            packet_len = real_len + 1;
            memcpy(buffer, remains.data(), packet_len);
            remains.erase(0, packet_len);
        }

        if(packet_len >= sizeof(PacketInput) && buffer[1] == 'I' && buffer[2] == 'N' && buffer[3] == 'P')
        {
            PacketInput *pkt = reinterpret_cast<PacketInput*>(buffer);
            memcpy(memory->airIoStatus, pkt->airIoStatus, sizeof(pkt->airIoStatus));
            memcpy(memory->sliderIoStatus, pkt->sliderIoStatus, sizeof(pkt->sliderIoStatus));
            memory->testBtn = pkt->testBtn;
            memory->serviceBtn = pkt->serviceBtn;
            current_packet_id = ntohl(pkt->packetId);
            updatePacketId(current_packet_id);
        }
        else if(packet_len >= sizeof(PacketInputNoAir) && buffer[1] == 'I' && buffer[2] == 'P' && buffer[3] == 'T') /// without air block
        {
            PacketInputNoAir *pkt = reinterpret_cast<PacketInputNoAir*>(buffer);
            memcpy(memory->sliderIoStatus, pkt->sliderIoStatus, sizeof(pkt->sliderIoStatus));
            memory->testBtn = pkt->testBtn;
            memory->serviceBtn = pkt->serviceBtn;
            current_packet_id = ntohl(pkt->packetId);
            updatePacketId(current_packet_id);
        }
        else if(packet_len >= sizeof(PacketFunction) && buffer[1] == 'F' && buffer[2] == 'N' && buffer[3] == 'C')
        {
            PacketFunction *pkt = reinterpret_cast<PacketFunction*>(buffer);
            switch(pkt->funcBtn)
            {
            case FUNCTION_COIN:
                memory->coinInsertion = 1;
                break;
            case FUNCTION_CARD:
                memory->cardRead = 1;
                break;
            }
        }
        else if(packet_len >= sizeof(PacketConnect) && buffer[1] == 'C' && buffer[2] == 'O' && buffer[3] == 'N')
        {
            last_input_packet_id = 0;
            PacketConnect *pkt = reinterpret_cast<PacketConnect*>(buffer);
            getSocksAddress(pkt, remote_address, remote_port);
            printErr("[INFO] Device %s:%d connected.\n", remote_address.data(), remote_port);
            CONNECTED = true;
        }
        else if(packet_len >= 4 && buffer[1] == 'D' && buffer[2] == 'I' && buffer[3] == 'S')
        {
            CONNECTED = false;
            if(tcp_mode)
            {
                EXIT_FLAG = true;
                printErr("[INFO] Device disconnected!\n");
                break;
            }
            if(!remote_address.empty())
            {
                printErr("[INFO] Device %s:%d disconnected.\n", remote_address.data(), remote_port);
                remote_address.clear();
            }
        }
        else if(packet_len >= sizeof(PacketPing) && buffer[1] == 'P' && buffer[2] == 'I' && buffer[3] == 'N')
        {
            if(!CONNECTED)
                continue;
            std::string response;
            response.assign(buffer, 12);
            response.replace(2, 1, "O");
            socketSendTo(sHost, addr, response);
        }
        else if(packet_len >= sizeof(PacketCard) && buffer[1] == 'C' && buffer[2] == 'R' && buffer[3] == 'D')
        {
            PacketCard *pkt = reinterpret_cast<PacketCard*>(buffer);
            static uint8_t lastId[10] = {};
            if(pkt->remoteCardRead)
            {
                if(memcmp(lastId, pkt->remoteCardId, 10))
                {
                    printErr("[INFO] Got remote card.\n");
                    printCardInfo(pkt->remoteCardType, pkt->remoteCardId);
                    memcpy(lastId, pkt->remoteCardId, 10);
                }
            }
            else
            {
                if(memory->remoteCardRead)
                {
                    printErr("[INFO] Remote card removed.\n");
                    memset(lastId, 0, 10);
                }
            }
            memory->remoteCardRead = pkt->remoteCardRead;
            memory->remoteCardType = pkt->remoteCardType;
            memcpy(memory->remoteCardId, pkt->remoteCardId, 10);
        }
    }
}

void printInfo()
{
    printf("=================================================\n");
    printf("=          Brokenithm-Evolved-Android:          =\n");
    printf("=     Brokenithm with full IO over network      =\n");
    printf("=               " VERSION " by XTindy                =\n");
    printf("=              Original: esterTion              =\n");
    printf("=================================================\n\n");
}

void checkArgs(int argc, char* argv[])
{
    int opt;
    while((opt = getopt(argc, argv, "p:Tr:")) != -1)
    {
        switch(opt)
        {
        case 'p':
            server_port = atoi(optarg);
            break;
        case 'T':
            tcp_mode = true;
            break;
        case 'r':
            tcp_receive_threshold = atoi(optarg);
            tcp_buffer_size = tcp_receive_threshold * 2;
            break;
        }
    }
}

int main(int argc, char* argv[])
{
    checkArgs(argc, argv);
    SetConsoleTitle("Brokenithm-Evolved-Android Server");
    printInfo();

    WSAData wsaData;
    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        //std::cerr << "WSA startup failed!\n";
        printErr("[ERROR] WSA startup failed!\n");
        return -1;
    }
    const char *memFileName = "Local\\BROKENITHM_SHARED_BUFFER";
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, false, memFileName);
    if(hMapFile == NULL)
    {
        hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 1024, memFileName);
        if(hMapFile == NULL)
        {
            //std::cerr << "CreateFileMapping failed! Error " + std::to_string(GetLastError());
            printErr("[ERROR] CreateFileMapping failed! error: %lu\n", GetLastError());
            return -1;
        }
    }
    defer(CloseHandle(hMapFile))
    IPCMemoryInfo *memory = reinterpret_cast<IPCMemoryInfo*>(MapViewOfFileEx(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 1024, NULL));
    if(memory == nullptr)
    {
        //std::cerr << "Cannot get view of memory map! Error " + std::to_string(GetLastError());
        printErr("[ERROR] Cannot get view of memory map! error: %lu\n", GetLastError());
        return -1;
    }
    if(!tcp_mode)
    {
        printErr("[INFO] Mode: UDP\n");
        SOCKET sHost = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        defer(closesocket(sHost))
        socketSetTimeout(sHost, 2000);
        socketBind(sHost, htonl(INADDR_ANY), server_port);
        printErr("[INFO] Waiting for device on port %d...\n", server_port);
        auto LEDThread = std::thread(threadLEDBroadcast, sHost, memory);
        auto InputThread = std::thread(threadInputReceive, sHost, memory);
        while(_getwch() != L'q');
        printErr("[INFO] Exiting gracefully...\n");
        last_input_packet_id = 0;
        EXIT_FLAG = true;
        LEDThread.join();
        InputThread.join();
    }
    else
    {
        printErr("[INFO] Mode: TCP\n");
        printErr("[INFO] TCP receive buffer size: %" PRIu32 "\n", tcp_buffer_size);
        printErr("[INFO] TCP receive threshold: %" PRIu32 "\n", tcp_receive_threshold);
        SOCKET sHost = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        defer(closesocket(sHost));
        socketSetTimeout(sHost, 50);
        socketBind(sHost, htonl(INADDR_ANY), server_port);
        listen(sHost, 10);
        while(true)
        {
            printErr("[INFO] Waiting for device on port %d...\n", server_port);
            struct sockaddr_in user_socket = {};
            socklen_t sock_size = sizeof(struct sockaddr_in);
            SOCKET acc_socket = accept(sHost, (struct sockaddr *)&user_socket, &sock_size);
            defer(closesocket(acc_socket));
            char buffer[20] = {};
            const char* user_address = inet_ntop(AF_INET, &user_socket.sin_addr, buffer, 20);
            if(user_address != NULL)
            {
                printErr("[INFO] Device %s:%d connected.\n", user_address, user_socket.sin_port);
            }
            CONNECTED = true;
            EXIT_FLAG = false;
            auto LEDThread = std::thread(threadLEDBroadcast, acc_socket, memory);
            auto InputThread = std::thread(threadInputReceive, acc_socket, memory);
            LEDThread.join();
            InputThread.join();
            printErr("[INFO] Exiting gracefully...\n");
            last_input_packet_id = 0;
            EXIT_FLAG = true;
            CONNECTED = false;
        }
    }
    return 0;
}
