#pragma once
#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ad {
inline std::wstring upper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return std::towupper(c); });
    return s;
}
struct Config {
    std::wstring prefix, fixed = L"000";
    int digits = 3;
    int maximum() const { int n = 1; for (int i = 0; i < digits; ++i) n *= 10; return n - 1; }
    void validate() const {
        auto valid = [](const std::wstring& s) {
            return std::all_of(s.begin(), s.end(), [](wchar_t c) {
                return (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-';
            });
        };
        if (prefix.empty() || !valid(upper(prefix)) || !valid(upper(fixed)) || digits < 1 || digits > 6)
            throw std::runtime_error("Use un prefijo valido, bloque fijo alfanumerico y entre 1 y 6 digitos.");
    }
};
struct Computer { std::wstring name, dns; };
enum class Network { Unchecked, Responding, NoReply, NoAddress, Error };
struct Row {
    std::wstring name, family, dns, ip;
    int number = 0;
    bool inAD = false;
    Network network = Network::Unchecked;
};
using Families = std::map<std::wstring, std::map<int, Computer>>;
struct Inventory { Families families; std::vector<Computer> unmatched; };
inline std::optional<std::pair<std::wstring,int>> parse(const std::wstring& raw, const Config& c) {
    const auto name = upper(raw), prefix = upper(c.prefix), fixed = upper(c.fixed);
    const size_t suffix = fixed.size() + static_cast<size_t>(c.digits);
    if (name.size() <= prefix.size() + suffix || name.compare(0, prefix.size(), prefix) != 0) return {};
    const size_t start = name.size() - c.digits;
    if (name.substr(start - fixed.size(), fixed.size()) != fixed) return {};
    int number = 0;
    for (size_t i = start; i < name.size(); ++i) {
        if (name[i] < L'0' || name[i] > L'9') return {};
        number = number * 10 + name[i] - L'0';
    }
    auto family = name.substr(prefix.size(), name.size() - prefix.size() - suffix);
    if (!std::all_of(family.begin(), family.end(), [](wchar_t ch) {
        return (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'-';
    })) return {};
    return std::make_pair(family, number);
}
inline Inventory classify(const std::vector<Computer>& computers, const Config& c) {
    c.validate(); Inventory result;
    for (const auto& pc : computers) {
        auto p = parse(pc.name, c);
        if (p) result.families[p->first][p->second] = pc;
        else result.unmatched.push_back(pc);
    }
    return result;
}
inline std::vector<Row> report(const Inventory& inventory, const Config& c,
                              std::optional<std::pair<int,int>> range) {
    if (range && (range->first < 0 || range->second > c.maximum() || range->first > range->second))
        throw std::runtime_error("Rango invalido para la cantidad de digitos configurada.");
    size_t total = 0;
    for (const auto& [family, pcs] : inventory.families) {
        (void)family;
        total += range ? range->second - range->first + 1 : pcs.rbegin()->first + 1;
    }
    if (total > 250000) throw std::runtime_error("El rango supera 250.000 filas. Reduzca el rango para continuar.");
    std::vector<Row> rows; rows.reserve(total);
    for (const auto& [family, pcs] : inventory.families) {
        int first = range ? range->first : 0, last = range ? range->second : pcs.rbegin()->first;
        for (int n = first; n <= last; ++n) {
            std::wostringstream name;
            name << upper(c.prefix) << family << upper(c.fixed) << std::setfill(L'0') << std::setw(c.digits) << n;
            auto pc = pcs.find(n);
            Row row; row.name = pc == pcs.end() ? name.str() : pc->second.name;
            row.family = family; row.number = n; row.inAD = pc != pcs.end();
            row.dns = row.inAD && !pc->second.dns.empty() ? pc->second.dns : row.name;
            rows.push_back(std::move(row));
        }
    }
    return rows;
}
inline std::wstring networkLabel(const Row& r) {
    switch (r.network) {
    case Network::Responding: return r.inAD ? L"Responde" : L"Conflicto: responde sin AD";
    case Network::NoReply: return L"Sin respuesta";
    case Network::NoAddress: return L"Sin IPv4 / DNS";
    case Network::Error: return L"Error de red";
    default: return L"Sin verificar";
    }
}
}
