#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/statvfs.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <iostream>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>

const char IP[] = "192.168.56.101";

struct ThreadArgs {
    int sock;
    sockaddr_in clientAddr;
    char recvBuf[4096];
};

struct DiskInfo {
    std::string device;
    std::string mountPoint;
    bool valid = false;
};

bool getDiskSpace(const char *path, uint64_t *freeBytes, uint64_t *usedBytes) {
    struct statvfs stat;
    // информацию о файловой системе
    if (statvfs(path, &stat) != 0) {
        perror("statvfs");
        return false;
    }

    // количество блоков для пользователя * размер блока
    *freeBytes = stat.f_bavail * stat.f_frsize;
    *usedBytes = (stat.f_blocks - stat.f_bfree) * stat.f_frsize;
    return true;
}

DiskInfo getDeviceInfo(const char* path) {
    DiskInfo info;

    // получение информации о пути
    struct stat pathStat;
    if (stat(path, &pathStat) != 0) {
        return info;
    }

    // чтение списка монтированных файловых систем
    std::ifstream mounts("/proc/mounts");
    if (!mounts) return info;

    std::string dev, mnt, fs, opts;
    while (mounts >> dev >> mnt >> fs >> opts) {
        struct stat mountStat;
        // если устройство точки монтирования совпадает с устройством пути
        if (stat(mnt.c_str(), &mountStat) == 0
            && pathStat.st_dev == mountStat.st_dev) {
            info.device = dev;
            info.mountPoint = mnt;
            info.valid = true;
            return info;
        }
        // пропуск оставшихся символов
        mounts.clear();
        mounts.ignore(10000, '\n');
    }
    return info;
}

uint64_t htonll(uint64_t val) {
    // преобразование 64-битного числа из little-endian в big-endian
    return (static_cast<uint64_t>(htonl(val & 0xFFFFFFFF)) << 32)
        | htonl(val >> 32);
}

void* handleClient(void* args) {
    std::cout << "Начало обработки сообщения потоком "
        << syscall(SYS_gettid) << std::endl;

    ThreadArgs* targs = static_cast<ThreadArgs*>(args);
    uint64_t freeBytes = 0, usedBytes = 0;
    uint64_t sendBuf[2] = {0, 0};

    #ifdef DEBUG
        DiskInfo di = getDeviceInfo(targs->recvBuf);
        if (di.valid) {
            std::cout << "Определено устройство: " << di.device
                << " -> " << di.mountPoint << std::endl;
        }
    #endif

    if (getDiskSpace(targs->recvBuf, &freeBytes, &usedBytes)) {
        sendBuf[0] = htonll(freeBytes);
        sendBuf[1] = htonll(usedBytes);
    }

    // отправка сообщения клиенту
    ssize_t bytesSent = sendto(
        targs->sock,
        sendBuf,
        sizeof(sendBuf),
        0,
        reinterpret_cast<sockaddr*>(&targs->clientAddr),
        sizeof(targs->clientAddr));

    if (bytesSent < 0) {
        perror("sendto");
    } else {
        std::cout << "Потоком " << syscall(SYS_gettid)
            << " отправлено сообщение: "
            << freeBytes << ", " << usedBytes << std::endl;
    }

    delete targs;
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

    // получение порта
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

    int sock;
    sockaddr_in addr;

    // создание сокета в Internet-домене (протокол IPv4)
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // адрес сокета (IPv4)
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);  // сетевой формат порта
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // связывание сокета с адресом
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    std::cout << "Сокет успешно создан по адресу "
        << IP << ":" << port << std::endl;

    while (true) {
        sockaddr_in clientAddr;
        char recvBuf[4096];

        socklen_t clientAddrLen = sizeof(clientAddr);

        // получение сообщения от клиента
        ssize_t bytesReceived = recvfrom(
            sock,
            recvBuf,
            sizeof(recvBuf) - 1,
            0,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientAddrLen);

        if (bytesReceived < 0) {
            perror("recvfrom");
            continue;
        }

        recvBuf[bytesReceived] = '\0';
        std::cout << "Получено сообщение от "
            << inet_ntoa(clientAddr.sin_addr)
            << ":" << ntohs(clientAddr.sin_port)
            << ": " << recvBuf << std::endl;

        // создание аргументов для потока
        ThreadArgs* targs = new ThreadArgs;
        targs->sock = sock;
        targs->clientAddr = clientAddr;
        memcpy(targs->recvBuf, recvBuf, bytesReceived + 1);

        // создание потока
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
