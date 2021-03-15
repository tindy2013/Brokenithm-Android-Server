#include <string>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>

#include "socket.h"
#include "defer.h"
#include "version.h"
#include <windows.h>

std::string remote_address;
uint16_t remote_port = 52468;
uint16_t server_port = 52468;
bool tcp_mode = false;

size_t tcp_buffer_size = 96;
size_t tcp_receive_threshold = 48;

std::atomic_bool EXIT_FLAG {false}, CONNECTED {false};

int setTimeout(SOCKET s, int timeout)
{
    int ret = -1;
#ifdef _WIN32
    ret = setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(int));
    ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(int));
#else
    struct timeval timeo = {timeout / 1000, (timeout % 1000) * 1000};
    ret = setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeo, sizeof(timeo));
    ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeo, sizeof(timeo));
#endif
    return ret;
}

int socket_bind(SOCKET sHost, long addr, uint16_t port)
{
    sockaddr_in srcaddr = {};
    memset(&srcaddr, 0, sizeof(srcaddr));
    srcaddr.sin_family = AF_INET;
    srcaddr.sin_addr.s_addr = addr;
    srcaddr.sin_port = htons(port);
    return bind(sHost, reinterpret_cast<sockaddr*>(&srcaddr), sizeof(srcaddr));
}

int udp_broadcast(SOCKET sHost, uint16_t port, const std::string &data)
{
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    addr.sin_port = htons(port);
    return sendto(sHost, data.data(), data.size(), 0, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
}

int udp_send(SOCKET sHost, const std::string &dst_host, uint16_t dst_port, const std::string &data)
{
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, dst_host.data(), (struct in_addr *)&addr.sin_addr.s_addr);
    addr.sin_port = htons(dst_port);
    return sendto(sHost, data.data(), data.size(), 0, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
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

void UDPLEDBroadcast(SOCKET sHost, const char* memory)
{
    static std::string previous_status;
    static int skip_count = 0;
    static std::string head = "\x63LED";
    while(!EXIT_FLAG)
    {
        if(!CONNECTED) {
            Sleep(50);
            continue;
        }
        std::string current_status;
        current_status.assign(reinterpret_cast<const char*>(memory + 6 + 32), 32 * 3);
        bool same = true;
        if(!previous_status.empty())
        {
            for(int i = 0; i < 32 * 3; i++)
            {
                if(previous_status[i] != current_status[i])
                {
                    same = false;
                    break;
                }
            }
        }
        else
            same = false;
        previous_status = current_status;
        if(!same)
        {
            current_status.insert(0, head);
            //if(udp_broadcast(sHost, server_port, current_status) < 0)
            if(udp_send(sHost, remote_address, remote_port, current_status) < 0)
            {
                //std::cerr<<"cannot send broadcast: error " + std::to_string(GetLastError()) + "\n";
                printErr("[ERROR] Cannot send packet: error %lu\n", GetLastError());
            }
            skip_count = 0;
        }
        else
        {
            if(++skip_count > 50)
            {
                current_status.insert(0, head);
                //if(udp_broadcast(sHost, server_port, current_status) < 0)
                if(udp_send(sHost, remote_address, remote_port, current_status) < 0)
                {
                    //std::cerr<<"cannot send broadcast: error " + std::to_string(GetLastError()) + "\n";
                    printErr("[ERROR] Cannot send packet: error %lu\n", GetLastError());
                }
                skip_count = 0;
            }
        }
        Sleep(10);
    }
}

void TCPLEDBroadcast(SOCKET sHost, const char* memory)
{
    static std::string previous_status;
    static int skip_count = 0;
    static std::string head = "\x63LED";
    while(!EXIT_FLAG)
    {
        if(!CONNECTED) {
            Sleep(50);
            continue;
        }
        std::string current_status;
        current_status.assign(reinterpret_cast<const char*>(memory + 6 + 32), 32 * 3);
        bool same = true;
        if(!previous_status.empty())
        {
            for(int i = 0; i < 32 * 3; i++)
            {
                if(previous_status[i] != current_status[i])
                {
                    same = false;
                    break;
                }
            }
        }
        else
            same = false;
        previous_status = current_status;
        if(!same)
        {
            current_status.insert(0, head);
            if(send(sHost, current_status.data(), current_status.size(), 0) < 0)
            {
                printErr("[Error] Cannot send packet: error %lu\n", GetLastError());
                if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    continue;
                }
                else
                {
                    printErr("[INFO] Device disconnected!");
                    CONNECTED = false;
                    EXIT_FLAG = true;
                    break;
                }
            }
            skip_count = 0;
        }
        else
        {
            if(++skip_count > 50)
            {
                current_status.insert(0, head);
                if(udp_send(sHost, remote_address, remote_port, current_status) < 0)
                {
                    printErr("[ERROR] Cannot send packet: error %lu\n", GetLastError());
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

std::tuple<std::string, uint16_t> getSocksAddress(const std::string &data)
{
    char cAddr[128] = {};
    std::string retAddr, port_str = data.substr(1, 2);
    int family = data[0];
    uint16_t port = ntohs(*(short*)port_str.data());
    switch(family)
    {
    case 1: //IPv4
        inet_ntop(AF_INET, data.data() + 3, cAddr, 127);
        break;
    case 2: //IPv6
        inet_ntop(AF_INET6, data.data() + 3, cAddr, 127);
        break;
    }
    retAddr.assign(cAddr);
    return std::make_tuple(retAddr, port);
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

void InputReceive(SOCKET sHost, char *memory)
{
    char recv_buffer[tcp_buffer_size];
    char buffer[BUFSIZ];
    std::string remains;
    while(!EXIT_FLAG)
    {
        int recv_len, real_len;
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
            size_t data_copied = real_len + 1;
            memcpy(buffer, remains.data(), data_copied);
            remains.erase(0, data_copied);
        }

        if(real_len >= 3 + 4 + 6 + 32 && buffer[1] == 'I' && buffer[2] == 'N' && buffer[3] == 'P')
        {
            memcpy(memory, buffer + 4 + 4, 32 + 6);
            if(real_len > 3 + 4 + 6 + 32)
            {
                memcpy(memory + 6 + 32 + 96, buffer + 4 + 4 + 6 + 32, real_len - (3 + 6 + 32 + 4));
            }
            current_packet_id = ntohl(*(uint32_t*)(buffer + 4));
            updatePacketId(current_packet_id);
        }
        else if(real_len >= 3 + 4 + 32 && buffer[1] == 'I' && buffer[2] == 'P' && buffer[3] == 'T') /// without air block
        {
            memcpy(memory, buffer + 4, 32);
            if(real_len > 3 + 4 + 32)
            {
                memcpy(memory + 6 + 32 + 96, buffer + 4 + 4 + 32, real_len - (3 + 32 + 4));
            }
            current_packet_id = ntohl(*(uint32_t*)(buffer + 4));
            updatePacketId(current_packet_id);
        }
        else if(real_len >= 4 && buffer[1] == 'F' && buffer[2] == 'N' && buffer[3] == 'C')
        {
            switch(buffer[4])
            {
            case FUNCTION_COIN:
                *(memory + 6 + 32 + 96 + 2) = 1;
                break;
            case FUNCTION_CARD:
                *(memory + 6 + 32 + 96 + 3) = 1;
                break;
            }
        }
        else if(real_len >= 10 && buffer[1] == 'C' && buffer[2] == 'O' && buffer[3] == 'N')
        {
            last_input_packet_id = 0;
            std::string data;
            data.assign(buffer + 4, real_len - 3);
            std::tie(remote_address, remote_port) = getSocksAddress(data);
            //std::cout << "Device " << remote_address << ":" << remote_port << " connected." <<std::endl;
            printErr("[INFO] Device %s:%d connected.\n", remote_address.data(), remote_port);
            CONNECTED = true;
        }
        else if(real_len >= 3 && buffer[1] == 'D' && buffer[2] == 'I' && buffer[3] == 'S')
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
                //std::cout << "Device " << remote_address << ":" << remote_port << " disconnected." << std::endl;
                remote_address.clear();
            }
        }
        else if(real_len >= 11 && buffer[1] == 'P' && buffer[2] == 'I' && buffer[3] == 'N')
        {
            if(!CONNECTED)
                continue;
            std::string response;
            response.assign(buffer, 12);
            response.replace(2, 1, "O");
            udp_send(sHost, remote_address, remote_port, response);
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
    char *memory = reinterpret_cast<char*>(MapViewOfFileEx(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 1024, NULL));
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
        setTimeout(sHost, 2000);
        int broadcastEnable = 1;
        setsockopt(sHost, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&broadcastEnable), sizeof(broadcastEnable));
        socket_bind(sHost, htonl(INADDR_ANY), server_port);
        //std::cout << "Waiting for device on port " << server_port << "..." << std::endl;
        printErr("[INFO] Waiting for device on port %d...\n", server_port);
        auto LEDThread = std::thread(UDPLEDBroadcast, sHost, memory);
        auto InputThread = std::thread(InputReceive, sHost, memory);
        while(_getwch() != L'q');
        //std::cout << "Exiting gracefully..." << std::endl;
        printErr("[INFO] Exiting gracefully...\n");
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
        setTimeout(sHost, 50);
        socket_bind(sHost, htonl(INADDR_ANY), server_port);
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
            auto LEDThread = std::thread(TCPLEDBroadcast, acc_socket, memory);
            auto InputThread = std::thread(InputReceive, acc_socket, memory);
            while(_getwch() != L'q');
            //std::cout << "Exiting gracefully..." << std::endl;
            printErr("[INFO] Exiting gracefully...\n");
            EXIT_FLAG = true;
            CONNECTED = false;
            LEDThread.join();
            InputThread.join();
        }
    }
    return 0;
}
