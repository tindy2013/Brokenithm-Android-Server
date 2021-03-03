#include <string>
#include <atomic>
#include <thread>
#include <unistd.h>

#include "socket.h"
#include "defer.h"
#include <windows.h>

std::string remote_address;
uint16_t remote_port = 52468;
uint16_t server_port = 52468;
bool tcp_mode = false;

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
                fprintf(stderr, "cannot send packet: error %lu\n", GetLastError());
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
                    fprintf(stderr, "cannot send packet: error %lu\n", GetLastError());
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
                fprintf(stderr, "cannot send packet: error %lu\n", GetLastError());
                if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    continue;
                }
                else
                {
                    fprintf(stderr, "Device disconnected!");
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
                    fprintf(stderr, "cannot send packet: error %lu\n", GetLastError());
                    if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                    {
                        continue;
                    }
                    else
                    {
                        fprintf(stderr, "Device disconnected!\n");
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

void InputReceive(SOCKET sHost, char *memory)
{
    char buffer[BUFSIZ];
    while(!EXIT_FLAG)
    {
        int recv_len;
        if((recv_len = recvfrom(sHost, buffer, BUFSIZ - 1, 0, NULL, NULL)) == -1)
            continue;
        int real_len = buffer[0];
        if(real_len > recv_len)
            continue;
        if(real_len >= 3 + 6 + 32 && buffer[1] == 'I' && buffer[2] == 'N' && buffer[3] == 'P')
        {
            memcpy(memory, buffer + 4, 6 + 32);
            if(real_len > 3 + 6 + 32)
            {
                memcpy(memory + 6 + 32 + 96, buffer + 4 + 6 + 32, real_len - (3 + 6 + 32));
            }
        }
        if(real_len >= 4 && buffer[1] == 'F' && buffer[2] == 'N' && buffer[3] == 'C')
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
        if(real_len >= 23 && buffer[1] == 'C' && buffer[2] == 'O' && buffer[3] == 'N')
        {
            remote_address.assign(buffer + 4, 15);
            auto epos = remote_address.find_last_not_of('\0');
            if(epos == std::string::npos)
                continue;
            remote_address.erase(epos + 1);
            std::string port;
            port.assign(buffer + 4 + 15, 5);
            remote_port = std::stoi(port);
            //std::cout << "Device " << remote_address << ":" << remote_port << " connected." <<std::endl;
            printf("Device %s:%d connected.\n", remote_address.data(), remote_port);
            CONNECTED = true;
        }
        if(real_len >= 3 && buffer[1] == 'D' && buffer[2] == 'I' && buffer[3] == 'S')
        {
            CONNECTED = false;
            if(tcp_mode)
            {
                EXIT_FLAG = true;
                printf("Device disconnected!\n");
                break;
            }
            if(!remote_address.empty())
            {
                printf("Device %s:%d disconnected.\n", remote_address.data(), remote_port);
                //std::cout << "Device " << remote_address << ":" << remote_port << " disconnected." << std::endl;
                remote_address.clear();
            }
        }
        if(real_len >= 11 && buffer[1] == 'P' && buffer[2] == 'I' && buffer[3] == 'N')
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
    printf("=                v0.1 by XTindy                 =\n");
    printf("=              Original: esterTion              =\n");
    printf("=================================================\n\n");
}

void checkArgs(int argc, char* argv[])
{
    int opt;
    while((opt = getopt(argc, argv, "p:T")) != -1)
    {
        switch(opt)
        {
        case 'p':
            server_port = atoi(optarg);
            break;
        case 'T':
            tcp_mode = true;
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
        fprintf(stderr, "WSA startup failed!\n");
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
            fprintf(stderr, "CreateFileMapping failed! error: %lu\n", GetLastError());
            return -1;
        }
    }
    defer(CloseHandle(hMapFile))
    char *memory = reinterpret_cast<char*>(MapViewOfFileEx(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 1024, NULL));
    if(memory == nullptr)
    {
        //std::cerr << "Cannot get view of memory map! Error " + std::to_string(GetLastError());
        fprintf(stderr, "Cannot get view of memory map! error: %lu\n", GetLastError());
        return -1;
    }
    if(!tcp_mode)
    {
        printf("Mode: UDP\n");
        SOCKET sHost = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        defer(closesocket(sHost))
        setTimeout(sHost, 2000);
        int broadcastEnable = 1;
        setsockopt(sHost, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&broadcastEnable), sizeof(broadcastEnable));
        socket_bind(sHost, htonl(INADDR_ANY), server_port);
        //std::cout << "Waiting for device on port " << server_port << "..." << std::endl;
        printf("Waiting for device on port %d...\n", server_port);
        auto LEDThread = std::thread(UDPLEDBroadcast, sHost, memory);
        auto InputThread = std::thread(InputReceive, sHost, memory);
        while(_getwch() != L'q');
        //std::cout << "Exiting gracefully..." << std::endl;
        printf("Exiting gracefully...\n");
        EXIT_FLAG = true;
        LEDThread.join();
        InputThread.join();
    }
    else
    {
        printf("Mode: TCP\n");
        SOCKET sHost = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        defer(closesocket(sHost));
        setTimeout(sHost, 50);
        socket_bind(sHost, htonl(INADDR_ANY), server_port);
        listen(sHost, 10);
        while(true)
        {
            printf("Waiting for device on port %d...\n", server_port);
            struct sockaddr_in user_socket = {};
            socklen_t sock_size = sizeof(struct sockaddr_in);
            SOCKET acc_socket = accept(sHost, (struct sockaddr *)&user_socket, &sock_size);
            defer(closesocket(acc_socket));
            char buffer[20] = {};
            const char* user_address = inet_ntop(AF_INET, &user_socket.sin_addr, buffer, 20);
            if(user_address != NULL)
            {
                printf("Device %s:%d connected.\n", user_address, user_socket.sin_port);
            }
            CONNECTED = true;
            EXIT_FLAG = false;
            auto LEDThread = std::thread(TCPLEDBroadcast, acc_socket, memory);
            auto InputThread = std::thread(InputReceive, acc_socket, memory);
            while(_getwch() != L'q');
            //std::cout << "Exiting gracefully..." << std::endl;
            printf("Exiting gracefully...\n");
            EXIT_FLAG = true;
            CONNECTED = false;
            LEDThread.join();
            InputThread.join();
        }
    }
    return 0;
}
