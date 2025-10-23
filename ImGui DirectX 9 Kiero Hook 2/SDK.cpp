#include "includes.h"
#include <Windows.h>
#include <fstream>
#include <string>
#include <ctime>
#include <cctype>
#include <direct.h> // for _mkdir
#include <set>
#include <map>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <functional>

// source sdk structures
class RecvTable;
class RecvProp;

class ClientClass {
public:
    void* pCreateFn;
    void* pCreateEventFn;
    char* pNetworkName;
    RecvTable* pRecvTable;
    ClientClass* pNext;
    int ClassID;
};

class RecvTable {
public:
    RecvProp* m_pProps;
    int m_nProps;
    void* m_pDecoder;
    char* m_pNetTableName;
    bool m_bInitialized;
    bool m_bInMainList;
};

class RecvProp {
public:
    char* m_pVarName;
    int m_RecvType;
    int m_Flags;
    int m_StringBufferSize;
    bool m_bInsideArray;
    const void* m_pExtraData;
    RecvProp* m_pArrayProp;
    void* m_ArrayLengthProxy;
    float m_fProxyFn;
    void* m_DataTableProxyFn;
    RecvTable* m_pDataTable;
    int m_Offset;
    int m_ElementStride;
    int m_nElements;
    const char* m_pParentArrayPropName;
};

typedef void* (*CreateInterfaceFn)(const char*, int*);

CreateInterfaceFn GetCreateInterface(const char* module) {
    HMODULE hMod = GetModuleHandleA(module);
    if (!hMod) return nullptr;
    return (CreateInterfaceFn)GetProcAddress(hMod, "CreateInterface");
}

void DumpRecvTable(RecvTable* table, int offset, std::ofstream& out, std::string parent = "", std::map<std::string, std::vector<std::string>>* tableMap = nullptr) {
    if (!table || !table->m_pNetTableName) return;

    for (int i = 0; i < table->m_nProps; ++i) {
        RecvProp* prop = &table->m_pProps[i];
        if (!prop->m_pVarName || std::isdigit(prop->m_pVarName[0]))
            continue;

        // only add parent if not recursing through baseclass
        std::string propName;
        if (strcmp(prop->m_pVarName, "baseclass") == 0)
            propName = parent;
        else
            propName = parent.empty() ? prop->m_pVarName : parent + "." + prop->m_pVarName;
        std::string tableName = table->m_pNetTableName;

        if (prop->m_nElements > 1 && prop->m_ElementStride > 0) {
            for (int j = 0; j < prop->m_nElements; ++j) {
                int arrOffset = offset + prop->m_Offset + j * prop->m_ElementStride;
                char buf[512];
                snprintf(buf, sizeof(buf), "Table: %s, Prop: %s[%d], Offset: 0x%X", tableName.c_str(), prop->m_pVarName, j, arrOffset);
                out << buf << std::endl;
                LOG_SDK(buf);
                if (tableMap) (*tableMap)[tableName].push_back(buf);
                if (prop->m_pDataTable && prop->m_pDataTable->m_pNetTableName) {
                    DumpRecvTable(prop->m_pDataTable, arrOffset, out, propName + "[" + std::to_string(j) + "]", tableMap);
                }
            }
        } else if (prop->m_pDataTable && prop->m_pDataTable->m_pNetTableName) {
            DumpRecvTable(prop->m_pDataTable, offset + prop->m_Offset, out, propName, tableMap);
        } else {
            char buf[512];
            snprintf(buf, sizeof(buf), "Table: %s, Prop: %s, Offset: 0x%X", tableName.c_str(), propName.c_str(), offset + prop->m_Offset);
            out << buf << std::endl;
            LOG_SDK(buf);
            if (tableMap) (*tableMap)[tableName].push_back(buf);
        }
    }
}

void DumpNetvars() {
    // 1. Get CreateInterface
    auto CreateInterface = GetCreateInterface("client.dll");
    if (!CreateInterface) {
        LOG_SDK("Failed to get CreateInterface from client.dll");
        return;
    }

    // 2. Get IBaseClientDLL
    void* pClient = CreateInterface("VClient017", 0);
    if (!pClient) {
        LOG_SDK("Failed to get VClient017 interface");
        return;
    }

    // 3. GetAllClasses
    using GetAllClassesFn = ClientClass* (__thiscall*)(void*);
    ClientClass* pClass = nullptr;
    void** vtable = *(void***)pClient;
    for (int idx : {8, 10}) {
        pClass = ((GetAllClassesFn)vtable[idx])(pClient);
        if (pClass) break;
    }
    if (!pClass) {
        LOG_SDK("Failed to get ClientClass list from VClient017");
        return;
    }

    // 4. Find game directory and create output folder
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH); // path to gmod.exe
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *lastSlash = '\0'; // remove gmod.exe, now exePath is the directory

    std::string outdir = std::string(exePath) + "\\output";
    _mkdir(outdir.c_str()); // create output folder if it doesn't exist

    std::string outpath = outdir + "\\netvars.txt";

    // 5. Walk and dump
    std::ofstream out(outpath);
    int count = 0;
    std::map<std::string, std::vector<std::string>> tableMap;
    for (ClientClass* cc = pClass; cc; cc = cc->pNext) {
        if (cc->pRecvTable) {
            DumpRecvTable(cc->pRecvTable, 0, out, "", &tableMap);
            ++count;
        }
    }
    out.close();

    // write each table to its own file
    for (const auto& pair : tableMap) {
        std::string filename = outdir + "\\" + pair.first + ".txt";
        std::ofstream tableOut(filename);
        for (const auto& line : pair.second) {
            tableOut << line << std::endl;
        }
        tableOut.close();
    }

    char doneMsg[512];
    snprintf(doneMsg, sizeof(doneMsg), "Netvar dump complete. Dumped %d tables. Saved to %s and per-table files.", count, outpath.c_str());
    LOG_SDK(doneMsg);
}

void InitializeGModSDK() {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    LOG_SDK("Initializing GMod SDK and dumping netvars...");
    DumpNetvars();
}

// =====================
// Pattern Scanner
// =====================
uintptr_t PatternScan(const char* module, const char* pattern) {
    HMODULE hMod = GetModuleHandleA(module);
    if (!hMod) return 0;
    auto dosHeader = (PIMAGE_DOS_HEADER)hMod;
    auto ntHeaders = (PIMAGE_NT_HEADERS)((uint8_t*)hMod + dosHeader->e_lfanew);
    size_t size = ntHeaders->OptionalHeader.SizeOfImage;
    uint8_t* data = (uint8_t*)hMod;

    std::vector<int> pat;
    std::istringstream iss(pattern);
    std::string byte;
    while (iss >> byte) {
        if (byte == "?" || byte == "??") pat.push_back(-1);
        else pat.push_back((int)strtol(byte.c_str(), nullptr, 16));
    }

    for (size_t i = 0; i < size - pat.size(); ++i) {
        bool found = true;
        for (size_t j = 0; j < pat.size(); ++j) {
            if (pat[j] != -1 && data[i + j] != pat[j]) {
                found = false;
                break;
            }
        }
        if (found) return (uintptr_t)&data[i];
    }
    return 0;
}

// =====================
// Netvar Accessor System
// =====================
static std::unordered_map<std::string, std::unordered_map<std::string, int>> g_NetvarMap;

void BuildNetvarMap(RecvTable* table, int offset = 0, std::string parent = "") {
    if (!table || !table->m_pNetTableName) return;
    std::string tableName = table->m_pNetTableName;
    for (int i = 0; i < table->m_nProps; ++i) {
        RecvProp* prop = &table->m_pProps[i];
        if (!prop->m_pVarName || std::isdigit(prop->m_pVarName[0])) continue;
        std::string propName;
        if (strcmp(prop->m_pVarName, "baseclass") == 0)
            propName = parent;
        else
            propName = parent.empty() ? prop->m_pVarName : parent + "." + prop->m_pVarName;
        if (prop->m_nElements > 1 && prop->m_ElementStride > 0) {
            for (int j = 0; j < prop->m_nElements; ++j) {
                int arrOffset = offset + prop->m_Offset + j * prop->m_ElementStride;
                std::string arrName = propName + "[" + std::to_string(j) + "]";
                g_NetvarMap[tableName][arrName] = arrOffset;
                if (prop->m_pDataTable && prop->m_pDataTable->m_pNetTableName) {
                    BuildNetvarMap(prop->m_pDataTable, arrOffset, arrName);
                }
            }
        } else if (prop->m_pDataTable && prop->m_pDataTable->m_pNetTableName) {
            BuildNetvarMap(prop->m_pDataTable, offset + prop->m_Offset, propName);
        } else {
            g_NetvarMap[tableName][propName] = offset + prop->m_Offset;
        }
    }
}

int GetNetvarOffset(const std::string& table, const std::string& prop) {
    auto t = g_NetvarMap.find(table);
    if (t == g_NetvarMap.end()) return 0;
    auto p = t->second.find(prop);
    if (p == t->second.end()) return 0;
    return p->second;
}

// =====================
// Interface Grabber
// =====================
void* GetInterface(const char* module, const char* name) {
    HMODULE hMod = GetModuleHandleA(module);
    if (!hMod) return nullptr;
    using CreateInterfaceFn = void* (*)(const char*, int*);
    auto CreateInterface = (CreateInterfaceFn)GetProcAddress(hMod, "CreateInterface");
    if (!CreateInterface) return nullptr;
    return CreateInterface(name, 0);
}

// =====================
// Vector/Math Helpers
// =====================
struct Vector {
    float x, y, z;
    Vector() : x(0), y(0), z(0) {}
    Vector(float x, float y, float z) : x(x), y(y), z(z) {}
    float Length() const { return sqrtf(x * x + y * y + z * z); }
    Vector operator-(const Vector& o) const { return Vector(x - o.x, y - o.y, z - o.z); }
    Vector operator+(const Vector& o) const { return Vector(x + o.x, y + o.y, z + o.z); }
    Vector operator*(float f) const { return Vector(x * f, y * f, z * f); }
    Vector& Normalize() { float l = Length(); if (l > 0) { x /= l; y /= l; z /= l; } return *this; }
};

// =====================
// Netvar Map Initialization (call after dump)
// =====================
void InitializeNetvarMap(ClientClass* pClass) {
    g_NetvarMap.clear();
    for (ClientClass* cc = pClass; cc; cc = cc->pNext) {
        if (cc->pRecvTable) {
            BuildNetvarMap(cc->pRecvTable, 0, "");
        }
    }
}