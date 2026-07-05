#include <iostream>
#include <cstdint>
#include <string>

#include <winsock2.h>
#include <ws2tcpip.h>

bool isValidPort(int port) {
    return port > 0 && port <= 65535;
}

int main(int argc, char *argv[]) {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    if (argc != 3) {
        std::cout << "Использование: " << argv[0]
            << " <IP сервера> <порт>" << std::endl;
        return 1;
    }

    int serverPort;
    try {
        serverPort = std::stoi(argv[2]);
        if (!isValidPort(serverPort)) {
            std::cerr << "Некорректное значение порта" << std::endl;
            return 1;
        }
    } catch (...) {
        std::cerr << "Некорректный формат порта" << std::endl;
        return 1;
    }

    const char* serverIp = argv[1];

    // проверка поддержки сокетов системой
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Ошибка инициализации Winsock: "
            << WSAGetLastError() << std::endl;
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Ошибка создания сокета: "
            << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    SOCKADDR_IN serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);

    int ret = inet_pton(AF_INET, serverIp, &serverAddr.sin_addr);
    if (ret <= 0) {
        if (ret == 0) {
            std::cerr << "Некорректный формат IP" << std::endl;
        } else {
            std::cerr << "Ошибка преобразования IP: "
                << WSAGetLastError() << std::endl;
        }
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    DWORD timeoutMs = 3000;
    if (setsockopt(
            sock,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeoutMs),
            sizeof(timeoutMs)) == SOCKET_ERROR) {
        std::cerr << "Не удалось установить таймаут на прием сообщения: "
            << WSAGetLastError() << std::endl;
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    while (true) {
        std::string diskPath;
        std::cout << "Введите путь: ";
        if (!std::getline(std::cin, diskPath)) break;

        if (diskPath.empty()) {
            std::cerr << "Путь не может быть пустым" << std::endl;
            continue;
        } else if (diskPath.size() > 4095) {
            std::cerr << "Путь слишком длинный, максимум 4095 символов"
                << std::endl;
            continue;
        }

        // отправка пути на сервер
        if (sendto(sock,
                diskPath.c_str(),
                diskPath.size(), 0,
                reinterpret_cast<SOCKADDR*>(&serverAddr),
                sizeof(serverAddr)) == SOCKET_ERROR) {
                std::cerr << "Ошибка отправки данных: "
                    << WSAGetLastError() << std::endl;
            continue;
        }

        // получение ответа
        uint64_t recvBuf[2] = {0, 0};
        SOCKADDR_IN fromAddr;
        int fromLen = sizeof(fromAddr);
        int bytesReceived = recvfrom(
            sock,
            reinterpret_cast<char*>(recvBuf),
            sizeof(recvBuf),
            0,
            reinterpret_cast<SOCKADDR*>(&fromAddr), &fromLen);

        if (bytesReceived == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                std::cerr << "Таймаут: сервер не ответил" << std::endl;
            } else {
                std::cerr << "Ошибка получения данных: " << err << std::endl;
            }
            continue;
        }

        std::cout << "Получено сообщение: свободно " << ntohll(recvBuf[0])
            << " байт, занято " << ntohll(recvBuf[1]) << " байт" << std::endl;
    }

    closesocket(sock);
    WSACleanup();

    return 0;
}
