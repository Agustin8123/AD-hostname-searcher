#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <activeds.h>
#include <memory>
#include "windows_services.h"

namespace ad {
namespace {
void check(HRESULT hr, const char* context) {
    if (FAILED(hr)) { std::ostringstream msg; msg << context << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ")."; throw std::runtime_error(msg.str()); }
}
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
};
struct Apartment {
    Apartment() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "No se pudo iniciar COM"); }
    ~Apartment() { CoUninitialize(); }
};
struct Search {
    IDirectorySearch* p; ADS_SEARCH_HANDLE h = nullptr;
    ~Search() { if (h) p->CloseSearchHandle(h); }
};
std::wstring column(IDirectorySearch* search, ADS_SEARCH_HANDLE h, const wchar_t* name) {
    ADS_SEARCH_COLUMN col{};
    HRESULT hr = search->GetColumn(h, const_cast<LPWSTR>(name), &col);
    if (hr == E_ADS_COLUMN_NOT_SET) return {};
    check(hr, "No se pudo leer un atributo de AD");
    std::wstring result;
    if (col.dwNumValues && col.dwADsType == ADSTYPE_CASE_IGNORE_STRING) result = col.pADsValues[0].CaseIgnoreString;
    search->FreeColumn(&col); return result;
}
}
std::vector<Computer> queryDirectory(std::atomic_bool& cancel) {
    Apartment apartment;
    Com<IADs> root;
    constexpr DWORD auth = ADS_SECURE_AUTHENTICATION | ADS_USE_SIGNING | ADS_USE_SEALING;
    check(ADsOpenObject(L"LDAP://RootDSE", nullptr, nullptr, auth, IID_IADs, reinterpret_cast<void**>(&root.p)),
          "No se pudo conectar al dominio. Verifique la conexion o VPN y su cuenta de dominio");
    VARIANT context; VariantInit(&context);
    BSTR property = SysAllocString(L"defaultNamingContext");
    HRESULT hr = root.p->Get(property, &context); SysFreeString(property);
    if (FAILED(hr)) { VariantClear(&context); check(hr, "No se pudo obtener el dominio"); }
    if (context.vt != VT_BSTR || !context.bstrVal) { VariantClear(&context); throw std::runtime_error("AD no devolvio un dominio valido."); }
    std::wstring path = L"LDAP://" + std::wstring(context.bstrVal); VariantClear(&context);
    if (cancel) return {};
    Com<IDirectorySearch> search;
    check(ADsOpenObject(path.c_str(), nullptr, nullptr, auth, IID_IDirectorySearch, reinterpret_cast<void**>(&search.p)), "No se pudo abrir la consulta AD");
    ADS_SEARCHPREF_INFO prefs[3]{};
    prefs[0].dwSearchPref = ADS_SEARCHPREF_PAGESIZE; prefs[0].vValue.dwType = ADSTYPE_INTEGER; prefs[0].vValue.Integer = 1000;
    prefs[1].dwSearchPref = ADS_SEARCHPREF_SEARCH_SCOPE; prefs[1].vValue.dwType = ADSTYPE_INTEGER; prefs[1].vValue.Integer = ADS_SCOPE_SUBTREE;
    prefs[2].dwSearchPref = ADS_SEARCHPREF_TIMEOUT; prefs[2].vValue.dwType = ADSTYPE_INTEGER; prefs[2].vValue.Integer = 15;
    check(search.p->SetSearchPreference(prefs, 3), "No se pudo configurar la consulta paginada");
    for (auto& pref : prefs) if (pref.dwStatus != ADS_STATUS_S_OK) throw std::runtime_error("AD rechazo la configuracion de busqueda.");
    LPWSTR attrs[] = {const_cast<LPWSTR>(L"name"), const_cast<LPWSTR>(L"dNSHostName")};
    Search handle{search.p};
    check(search.p->ExecuteSearch(const_cast<LPWSTR>(L"(objectCategory=computer)"), attrs, 2, &handle.h), "Fallo la consulta AD");
    std::vector<Computer> computers;
    while (!cancel) {
        hr = search.p->GetNextRow(handle.h);
        if (hr == S_ADS_NOMORE_ROWS) break;
        if (hr != S_OK) { check(hr, "AD devolvio una consulta incompleta"); throw std::runtime_error("AD devolvio una consulta incompleta; vuelva a intentar."); }
        auto name = column(search.p, handle.h, L"name");
        if (name.empty()) throw std::runtime_error("AD devolvio un equipo sin nombre; se descarta la consulta incompleta.");
        computers.push_back({name, column(search.p, handle.h, L"dNSHostName")});
    }
    return computers;
}
void ping(Row& row) {
    ADDRINFOW hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    ADDRINFOW* addresses = nullptr;
    int error = GetAddrInfoW(row.dns.c_str(), nullptr, &hints, &addresses);
    if (error || !addresses) { row.network = (error == WSAHOST_NOT_FOUND || error == WSANO_DATA) ? Network::NoAddress : Network::Error; return; }
    std::unique_ptr<ADDRINFOW, decltype(&FreeAddrInfoW)> cleanup(addresses, FreeAddrInfoW);
    HANDLE icmp = IcmpCreateFile();
    row.network = Network::NoReply;
    for (auto* address = addresses; address; address = address->ai_next) {
        auto ip = reinterpret_cast<sockaddr_in*>(address->ai_addr)->sin_addr;
        wchar_t text[INET_ADDRSTRLEN]{}; InetNtopW(AF_INET, &ip, text, INET_ADDRSTRLEN);
        if (!row.ip.empty()) row.ip += L", ";
        row.ip += text;
        if (icmp == INVALID_HANDLE_VALUE) { row.network = Network::Error; continue; }
        char data[] = "AD hostname check";
        alignas(ICMP_ECHO_REPLY) unsigned char reply[sizeof(ICMP_ECHO_REPLY) + sizeof(data) + 8]{};
        DWORD count = IcmpSendEcho(icmp, ip.s_addr, data, sizeof(data), nullptr, reply, sizeof(reply), 500);
        if (count && reinterpret_cast<ICMP_ECHO_REPLY*>(reply)->Status == IP_SUCCESS) row.network = Network::Responding;
        else if (!count && GetLastError() != IP_REQ_TIMED_OUT && row.network != Network::Responding) row.network = Network::Error;
    }
    if (icmp != INVALID_HANDLE_VALUE) IcmpCloseHandle(icmp);
}
}
