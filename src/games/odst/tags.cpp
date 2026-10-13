#include "tags.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "offsets.h"

namespace {
    char* g_base = nullptr;  // halo3odst.dll

    struct TagInstance {
        uint16_t group;
        uint16_t salt;
        uint32_t address;
    };

    struct GlobalTag {
        uint32_t groupMagic;
        uint32_t datum;
    };

    char* Header() { return g_base ? *(char**)(g_base + OFFSET_ODST_TAG_INDEX_HEADER) : nullptr; }

    char* Resolve(uint32_t address) {
        char* tagBase = *(char**)(g_base + OFFSET_ODST_TAG_BASE);
        return address && tagBase ? tagBase + (uint64_t)address * 4 : nullptr;
    }
}

void OdstTags::SetModule(HMODULE odst) {
    g_base = (char*)odst;
}

char* OdstTags::Get(uint32_t datum) {
    char* header = Header();
    if (!header || datum == 0xFFFFFFFF) return nullptr;

    uint16_t index = datum & 0xFFFF;
    if (index >= *(int32_t*)(header + ODST_TAG_HEADER_TAG_COUNT)) return nullptr;

    auto instances = *(TagInstance**)(header + ODST_TAG_HEADER_INSTANCES);
    if (!instances || instances[index].salt != (datum >> 16)) return nullptr;
    return Resolve(instances[index].address);
}

char* OdstTags::GetGlobal(uint32_t groupMagic) {
    char* header = Header();
    if (!header) return nullptr;

    int count = *(int32_t*)(header + ODST_TAG_HEADER_GLOBAL_COUNT);
    auto globals = *(GlobalTag**)(header + ODST_TAG_HEADER_GLOBALS);
    for (int i = 0; globals && i < count && i < 64; ++i)
        if (globals[i].groupMagic == groupMagic) return Get(globals[i].datum);
    return nullptr;
}

int OdstTags::BlockCount(char* tag, int blockOffset) {
    return tag ? *(int32_t*)(tag + blockOffset) : 0;
}

char* OdstTags::BlockElement(char* tag, int blockOffset, int elementSize, int index) {
    if (!tag || index < 0 || index >= BlockCount(tag, blockOffset)) return nullptr;
    char* elements = Resolve(*(uint32_t*)(tag + blockOffset + 4));
    return elements ? elements + (int64_t)elementSize * index : nullptr;
}

namespace {
    // Tag names by tag index, read from the current .map file.
    std::vector<std::string> g_names;
    std::string g_namesMap;
    // String ids: all strings' text, each string's offset, and the count per namespace.
    std::vector<char> g_stringData;
    std::vector<int32_t> g_stringOffsets;
    std::vector<int32_t> g_namespaces;

    struct RawTag {
        uint32_t datum;
        uint16_t index;
    };

    // Copies matching tags into `out` under SEH (tag memory can vanish
    // mid-read during a map change). Returns how many were found.
    int CollectGroup(uint32_t groupMagic, RawTag* out, int max) {
        __try {
            char* header = Header();
            if (!header) return 0;

            int count = *(int32_t*)(header + ODST_TAG_HEADER_TAG_COUNT);
            auto instances = *(TagInstance**)(header + ODST_TAG_HEADER_INSTANCES);
            auto groups = *(uint32_t(**)[4])(header + ODST_TAG_HEADER_GROUPS);  // {magic, parent, grandparent, name}
            if (!instances || !groups) return 0;

            int n = 0;
            for (int i = 0; i < count && i < 0x10000 && n < max; ++i) {
                const TagInstance& tag = instances[i];
                if (tag.address == 0 || tag.group == 0xFFFF || groups[tag.group][0] != groupMagic) continue;
                out[n].datum = ((uint32_t)tag.salt << 16) | (uint32_t)i;
                out[n].index = (uint16_t)i;
                ++n;
            }
            return n;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // <MCC root>\halo3odst\maps\<map>.map, from the exe at <root>\mcc\binaries\win64.
    std::wstring MapPath(const std::string& mapName) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring root(exe);
        for (int up = 0; up < 4; ++up) root = root.substr(0, root.find_last_of(L'\\'));
        return root + L"\\halo3odst\\maps\\" + std::wstring(mapName.begin(), mapName.end()) + L".map";
    }

    uint32_t U32(const std::vector<char>& buf, size_t offset) { return *(const uint32_t*)(buf.data() + offset); }

    // Reads the string id tables from an open .map file.
    bool ReadStrings(FILE* file, const std::vector<char>& header, uint32_t mask) {
        uint32_t count = U32(header, ODST_MAP_STRING_COUNT);
        uint32_t size = U32(header, ODST_MAP_STRING_DATA_SIZE);
        uint32_t spaces = U32(header, ODST_MAP_STRING_NAMESPACE_COUNT);
        if (count == 0 || count > 0x100000 || size == 0 || size > 0x4000000 || spaces == 0 || spaces > 256)
            return false;
        g_stringOffsets.resize(count);
        g_stringData.resize(size + 1);
        g_namespaces.resize(spaces);
        return _fseeki64(file, (uint32_t)(U32(header, ODST_MAP_STRING_INDEX) + mask), SEEK_SET) == 0 &&
               fread(g_stringOffsets.data(), 4, count, file) == count &&
               _fseeki64(file, (uint32_t)(U32(header, ODST_MAP_STRING_DATA) + mask), SEEK_SET) == 0 &&
               fread(g_stringData.data(), 1, size, file) == size &&
               _fseeki64(file, (uint32_t)(U32(header, ODST_MAP_STRING_NAMESPACES) + mask), SEEK_SET) == 0 &&
               fread(g_namespaces.data(), 4, spaces, file) == spaces;
    }
}

bool OdstTags::LoadNames() {
    char name[32] = {};
    if (g_base) memcpy(name, g_base + OFFSET_ODST_MAP_HEADER + ODST_MAP_INTERNAL_NAME, sizeof(name) - 1);
    std::string mapName = name;
    if (mapName.empty()) {
        LOG("ODST: no map name in the map header; can't read tag names");
        return false;
    }
    if (mapName == g_namesMap) return !g_names.empty();

    g_names.clear();
    g_stringOffsets.clear();
    g_namesMap = mapName;

    std::wstring path = MapPath(mapName);
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
        LOG("ODST: can't open %ls for tag names", path.c_str());
        return false;
    }

    std::vector<char> header(ODST_MAP_HEADER_SIZE);
    bool ok = fread(header.data(), 1, header.size(), file) == header.size() && U32(header, 0) == 0x68656164;  // 'head'
    uint32_t count = ok ? U32(header, ODST_MAP_FILE_TABLE_COUNT) : 0;
    uint32_t size = ok ? U32(header, ODST_MAP_FILE_TABLE_SIZE) : 0;
    uint32_t mask = ok ? U32(header, ODST_MAP_OFFSET_MASK0) : 0;
    uint32_t tableAt = U32(header, ODST_MAP_FILE_TABLE_OFFSET) + mask;  // wraps on purpose
    uint32_t indexAt = U32(header, ODST_MAP_FILE_INDEX_OFFSET) + mask;
    ok = ok && count > 0 && count < 0x10000 && size > 0 && size < 0x1000000;

    std::vector<int32_t> offsets(ok ? count : 0);
    std::vector<char> strings(ok ? size + 1 : 0);
    ok = ok && _fseeki64(file, indexAt, SEEK_SET) == 0 &&
         fread(offsets.data(), 4, count, file) == count && _fseeki64(file, tableAt, SEEK_SET) == 0 &&
         fread(strings.data(), 1, size, file) == size;
    if (ok && !ReadStrings(file, header, mask)) {
        g_stringOffsets.clear();
        LOG("ODST: couldn't read string ids from %ls", path.c_str());
    }
    fclose(file);

    if (!ok) {
        LOG("ODST: couldn't read tag names from %ls", path.c_str());
        return false;
    }

    g_names.resize(count);
    for (uint32_t i = 0; i < count; ++i)
        if (offsets[i] >= 0 && (uint32_t)offsets[i] < size) g_names[i] = strings.data() + offsets[i];
    LOG("ODST: read %u tag names from %s.map", count, mapName.c_str());
    return true;
}

std::vector<Games::TagChoice> OdstTags::ListGroup(uint32_t groupMagic) {
    static RawTag raw[1024];
    int n = CollectGroup(groupMagic, raw, _countof(raw));

    std::vector<Games::TagChoice> out;
    for (int i = 0; i < n; ++i) {
        std::string name = raw[i].index < g_names.size() ? g_names[raw[i].index] : "";
        size_t slash = name.find_last_of('\\');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (name.empty()) name = "tag " + std::to_string(raw[i].index);
        out.push_back({raw[i].datum, name});
    }
    std::sort(out.begin(), out.end(),
              [](const Games::TagChoice& a, const Games::TagChoice& b) { return a.name < b.name; });
    return out;
}

uint32_t OdstTags::GroupOf(uint32_t datum) {
    __try {
        char* header = Header();
        if (!header || datum == 0xFFFFFFFF) return 0;
        uint16_t index = datum & 0xFFFF;
        if (index >= *(int32_t*)(header + ODST_TAG_HEADER_TAG_COUNT)) return 0;
        auto instances = *(TagInstance**)(header + ODST_TAG_HEADER_INSTANCES);
        auto groups = *(uint32_t(**)[4])(header + ODST_TAG_HEADER_GROUPS);
        if (!instances || !groups || instances[index].salt != (datum >> 16) || instances[index].group == 0xFFFF)
            return 0;
        return groups[instances[index].group][0];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

std::string OdstTags::FullName(uint32_t datum) {
    uint16_t index = datum & 0xFFFF;
    return datum != 0xFFFFFFFF && index < g_names.size() ? g_names[index] : std::string();
}

std::string OdstTags::StringName(uint32_t stringId) {
    if (g_stringOffsets.empty() || g_namespaces.empty()) return std::string();
    uint32_t space = (stringId >> 17) & 0xFF;
    uint32_t index = stringId & 0x1FFFF;
    uint64_t global = index;
    if (space == 0) {
        if (index >= (uint32_t)g_namespaces[0]) {  // stored after every namespace
            global = index - g_namespaces[0];
            for (int32_t n : g_namespaces) global += n;
        }
    } else {
        if (space >= g_namespaces.size()) return std::string();
        for (uint32_t n = 0; n < space; ++n) global += g_namespaces[n];
    }
    if (global >= g_stringOffsets.size()) return std::string();
    int32_t offset = g_stringOffsets[global];
    if (offset < 0 || (size_t)offset >= g_stringData.size() - 1) return std::string();
    return g_stringData.data() + offset;
}
