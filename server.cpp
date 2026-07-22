#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <pthread.h>

#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/stat.h>

#include <iostream>
#include <cstring>
#include <fstream>
#include <string>

const char* IP = "127.0.0.1";
constexpr size_t RecvBufSize = 4096;

struct ThreadArgs {
    int sock = -1;
    sockaddr_in clientAddr{};
    char recvBuf[RecvBufSize];
};

struct DiskInfo {
    std::string device = "";
    std::string mountPoint = "";
};

bool getDiskSpace(const char *path, uint64_t &freeBytes, uint64_t &usedBytes) {
    struct statvfs stat;
    // информация о файловой системе
    if (statvfs(path, &stat) != 0) {
        perror("statvfs");
        return false;
    }

    freeBytes = stat.f_bavail * stat.f_frsize;
    usedBytes = (stat.f_blocks - stat.f_bfree) * stat.f_frsize;

    return true;
}

bool getDeviceInfo(const char* path, DiskInfo &info) {
    // информации о пути
    struct stat pathStat;
    if (stat(path, &pathStat) != 0) {
        return false;
    }

    // чтение списка монтированных файловых систем
    std::ifstream mounts("/proc/mounts");
    if (!mounts) return false;

    DiskInfo foundInfo{};
    std::string fs, opts;
    while (mounts >> foundInfo.device >> foundInfo.mountPoint >> fs >> opts) {
        struct stat mountStat;
        // если устройство точки монтирования совпадает с устройством пути
        if (stat(foundInfo.mountPoint.c_str(), &mountStat) == 0 &&
                pathStat.st_dev == mountStat.st_dev) {
            info = foundInfo;
            return true;
        }
        
        mounts.clear();
        mounts.ignore(10000, '\n');
    }

    return false;
}

uint64_t htonll(uint64_t num) {
    static constexpr int Half = 32;
    return (static_cast<uint64_t>(htonl(num & 0xFFFFFFFF)) << Half) |
        htonl(num >> Half);
}

void* handleClient(void* args) {
    std::cout << "Начало обработки сообщения потоком "
        << syscall(SYS_gettid) << std::endl;

    auto* tArgs = static_cast<ThreadArgs*>(args);
    uint64_t sendBuf[2] = {0, 0};

    #ifdef DEBUG
        DiskInfo info;
        bool ok = getDeviceInfo(targs->recvBuf, info);
        if (!ok) {
            std::cout << "Определено устройство: " << info.device <<
                " -> " << info.mountPoint << std::endl;
        }
    #endif

    if (getDiskSpace(tArgs->recvBuf, sendBuf[0], sendBuf[1])) {
        sendBuf[0] = htonll(sendBuf[0]);
        sendBuf[1] = htonll(sendBuf[1]);
    }

    ssize_t bytesSent = sendto(
        tArgs->sock,
        sendBuf,
        sizeof(sendBuf),
        0,
        reinterpret_cast<sockaddr*>(&tArgs->clientAddr),
        sizeof(tArgs->clientAddr));

    if (bytesSent < 0) {
        perror("sendto");
    } else {
        std::cout << "Потоком " << syscall(SYS_gettid) <<
            " отправлено сообщение: " <<
            sendBuf[0] << ", " << sendBuf[1] << std::endl;
    }

    delete tArgs;
    return nullptr;
}

bool isValidPort(int port) {
    return port > 0 && port <= 65535;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cout << "Использование: " << argv[0] << " <порт>" << std::endl;
        return 1;
    }

    int port;
    try {
        port = std::stoi(argv[1]);
        if (!isValidPort(port)) {
            std::cerr << "Некорректное значение порта" << std::endl;
            return 1;
        }
    } catch (...) {
        std::cerr << "Некорректный формат порта" << std::endl;
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    std::cout << "Сокет успешно создан по адресу " <<
        IP << ":" << port << std::endl;

    while (true) {
        sockaddr_in clientAddr{};
        char recvBuf[RecvBufSize];

        socklen_t clientAddrLen = sizeof(clientAddr);
        ssize_t bytesReceived = recvfrom(
            sock,
            recvBuf,
            sizeof(recvBuf) - 1,
            0,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientAddrLen
        );

        if (bytesReceived < 0) {
            perror("recvfrom");
            continue;
        }

        recvBuf[bytesReceived] = '\0';

        std::cout << "Получено сообщение от " <<
            inet_ntoa(clientAddr.sin_addr) <<
            ":" << ntohs(clientAddr.sin_port) <<
            ": " << recvBuf << std::endl;

        ThreadArgs* targs = new ThreadArgs;
        targs->sock = sock;
        targs->clientAddr = clientAddr;
        memcpy(targs->recvBuf, recvBuf, bytesReceived + 1);

        pthread_t tid;
        if (pthread_create(&tid, nullptr, handleClient, targs) != 0) {
            std::cerr << "Не удалось создать поток" << std::endl;
            delete targs;
            continue;
        }
        
        if (pthread_detach(tid) != 0) {
            std::cerr << "Не удалось отделить поток" << std::endl;
        }
    }

    close(sock);

    return 0;
}
