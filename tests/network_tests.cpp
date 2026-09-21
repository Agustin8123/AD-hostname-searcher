#include <winsock2.h>
#include "../src/windows_services.h"
#include <iostream>
int main() {
    WSADATA data{};
    if(WSAStartup(MAKEWORD(2,2),&data)) return 1;
    ad::Row row; row.dns=L"127.0.0.1"; row.inAD=true;
    ad::ping(row);
    bool passed=row.ip==L"127.0.0.1" && row.network==ad::Network::Responding;
    WSACleanup();
    std::cout << (passed ? "Loopback ping and IP passed\n" : "Loopback ping or IP failed\n");
    return passed ? 0 : 1;
}
