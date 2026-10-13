#include "tags.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "miniz.h"

#include "core/log.h"
#include "offsets.h"

namespace {
    char* g_base = nullptr;  // halo2.dll

    char* Entries() { return *(char**)(g_base + OFFSET_HALO2_TAG_ENTRIES); }
    char* TagBase() { return *(char**)(g_base + OFFSET_HALO2_TAG_BASE); }
    char* Header() { return *(char**)(g_base + OFFSET_HALO2_MAP_HEADER); }

    // The loaded map's tag count (the header's file table count).
    int TagCount() {
        char* header = Header();
        return header ? *(int32_t*)(header + HALO2_MAP_FILE_TABLE_COUNT) : 0;
    }

    char* Entry(int index) {
        char* entries = Entries();
        return entries && index >= 0 && index < TagCount() ? entries + index * HALO2_TAG_ENTRY_SIZE : nullptr;
    }

    char* AtOffset(int32_t offset) {
        if (offset < 0) {  // the top bit picks the second base
            char* base = *(char**)(g_base + OFFSET_HALO2_TAG_BASE_2);
            return base ? base + (offset & 0x7FFFFFFF) : nullptr;
        }
        char* base = TagBase();
        return base && offset ? base + offset : nullptr;
    }
}

void Halo2Tags::SetModule(HMODULE halo2) {
    g_base = (char*)halo2;
}

char* Halo2Tags::Get(uint32_t datum) {
    if (!g_base || datum == 0xFFFFFFFF) return nullptr;
    char* entry = Entry(datum & 0xFFFF);
    if (!entry || *(uint32_t*)(entry + HALO2_TAG_ENTRY_DATUM) != datum) return nullptr;
    return AtOffset(*(int32_t*)(entry + HALO2_TAG_ENTRY_OFFSET));
}

uint32_t Halo2Tags::GroupOf(uint32_t datum) {
    if (!g_base || datum == 0xFFFFFFFF) return 0;
    char* entry = Entry(datum & 0xFFFF);
    if (!entry || *(uint32_t*)(entry + HALO2_TAG_ENTRY_DATUM) != datum) return 0;
    return *(uint32_t*)entry;
}

char* Halo2Tags::First(uint32_t groupMagic) {
    if (!g_base) return nullptr;
    int count = TagCount();
    for (int i = 0; i < count; ++i) {
        char* entry = Entry(i);
        if (entry && *(uint32_t*)entry == groupMagic) return AtOffset(*(int32_t*)(entry + HALO2_TAG_ENTRY_OFFSET));
    }
    return nullptr;
}

int Halo2Tags::BlockCount(char* tag, int blockOffset) {
    return tag ? *(int32_t*)(tag + blockOffset) : 0;
}

char* Halo2Tags::BlockElement(char* tag, int blockOffset, int elementSize, int index) {
    if (!tag || index < 0 || index >= BlockCount(tag, blockOffset)) return nullptr;
    char* elements = AtOffset(*(int32_t*)(tag + blockOffset + 4));
    return elements ? elements + (int64_t)elementSize * index : nullptr;
}

namespace {
    std::vector<std::string> g_names;  // by tag index
    std::vector<std::string> g_strings;  // by string id index
    std::string g_namesMap;

    struct RawTag {
        uint32_t datum;
        uint16_t index;
    };

    int Collect(uint32_t groupMagic, RawTag* out, int max) {
        __try {
            int count = TagCount();
            int n = 0;
            for (int i = 0; i < count && i < 0x10000 && n < max; ++i) {
                char* entry = Entry(i);
                if (!entry || *(uint32_t*)entry != groupMagic) continue;
                out[n].datum = *(uint32_t*)(entry + HALO2_TAG_ENTRY_DATUM);
                out[n].index = (uint16_t)i;
                ++n;
            }
            return n;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    bool MapName(char* out, size_t size) {
        __try {
            char* header = Header();
            if (!header) return false;
            memcpy(out, header + HALO2_MAP_INTERNAL_NAME, size - 1);
            out[size - 1] = 0;
            return out[0] != 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // <MCC root>\halo2\h2_maps_win64_dx11\<map>.map
    std::wstring MapPath(const std::string& mapName) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring root(exe);
        for (int up = 0; up < 4; ++up) root = root.substr(0, root.find_last_of(L'\\'));
        return root + L"\\halo2\\h2_maps_win64_dx11\\" + std::wstring(mapName.begin(), mapName.end()) + L".map";
    }

    // Reads ranges of a chunk-compressed .map file's decompressed data.
    class CompressedMap {
    public:
        explicit CompressedMap(FILE* file) : m_file(file) {}

        bool Init() {
            m_header.resize(HALO2_MAP_HEADER_SIZE);
            if (fread(m_header.data(), 1, m_header.size(), m_file) != m_header.size()) return false;
            m_chunkSize = U32(HALO2_MAP_CHUNK_SIZE);
            uint32_t count = U32(HALO2_MAP_CHUNK_COUNT);
            if (m_chunkSize == 0 || m_chunkSize > 0x1000000 || count == 0 || count > 0x10000) return false;
            m_chunks.resize(count);
            return _fseeki64(m_file, U32(HALO2_MAP_CHUNK_TABLE), SEEK_SET) == 0 &&
                   fread(m_chunks.data(), sizeof(Chunk), count, m_file) == count;
        }

        uint32_t U32(size_t offset) const { return *(const uint32_t*)(m_header.data() + offset); }

        // `offset` is a header file offset (it counts the header).
        bool Read(uint32_t offset, char* out, size_t size) {
            if (offset < HALO2_MAP_HEADER_SIZE) return false;
            uint64_t at = offset - HALO2_MAP_HEADER_SIZE;
            while (size > 0) {
                const std::vector<char>* chunk = Decompressed(at / m_chunkSize);
                if (!chunk) return false;
                size_t inChunk = (size_t)(at % m_chunkSize);
                if (inChunk >= chunk->size()) return false;
                size_t take = std::min(size, chunk->size() - inChunk);
                memcpy(out, chunk->data() + inChunk, take);
                out += take;
                size -= take;
                at += take;
            }
            return true;
        }

    private:
        struct Chunk {
            int32_t compressedSize;
            int32_t fileOffset;
        };

        const std::vector<char>* Decompressed(uint64_t index) {
            if (index >= m_chunks.size()) return nullptr;
            auto found = m_cache.find(index);
            if (found != m_cache.end()) return &found->second;
            const auto& c = m_chunks[(size_t)index];
            if (c.compressedSize <= 0) return nullptr;
            std::vector<unsigned char> packed(c.compressedSize);
            if (_fseeki64(m_file, c.fileOffset, SEEK_SET) != 0 ||
                fread(packed.data(), 1, packed.size(), m_file) != packed.size())
                return nullptr;
            std::vector<char> data(m_chunkSize);
            mz_ulong length = m_chunkSize;
            if (mz_uncompress((unsigned char*)data.data(), &length, packed.data(), (mz_ulong)packed.size()) != MZ_OK)
                return nullptr;
            data.resize(length);
            return &(m_cache[index] = std::move(data));
        }

        FILE* m_file;
        std::vector<char> m_header;
        uint32_t m_chunkSize = 0;
        std::vector<Chunk> m_chunks;
        std::map<uint64_t, std::vector<char>> m_cache;
    };
}

bool Halo2Tags::LoadNames() {
    char name[32] = {};
    if (!g_base || !MapName(name, sizeof(name))) {
        LOG("Halo 2: no map name in the map header; can't read tag names");
        return false;
    }
    std::string mapName = name;
    if (mapName == g_namesMap) return !g_names.empty();
    g_names.clear();
    g_strings.clear();
    g_namesMap = mapName;

    std::wstring path = MapPath(mapName);
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
        LOG("Halo 2: can't open %ls for tag names", path.c_str());
        return false;
    }

    CompressedMap map(file);
    bool ok = map.Init();
    uint32_t count = ok ? map.U32(HALO2_MAP_FILE_TABLE_COUNT) : 0;
    uint32_t size = ok ? map.U32(HALO2_MAP_FILE_TABLE_SIZE) : 0;
    ok = ok && count > 0 && count < 0x10000 && size > 0 && size < 0x1000000;
    std::vector<int32_t> offsets(ok ? count : 0);
    std::vector<char> strings(ok ? size + 1 : 0);
    ok = ok && map.Read(map.U32(HALO2_MAP_FILE_INDEX_OFFSET), (char*)offsets.data(), count * 4) &&
         map.Read(map.U32(HALO2_MAP_FILE_TABLE_OFFSET), strings.data(), size);

    // String ids (model variant names), same layout.
    uint32_t idCount = ok ? map.U32(HALO2_MAP_STRING_COUNT) : 0;
    uint32_t idSize = ok ? map.U32(HALO2_MAP_STRING_TABLE_SIZE) : 0;
    if (idCount > 0 && idCount < 0x100000 && idSize > 0 && idSize < 0x4000000) {
        std::vector<int32_t> idOffsets(idCount);
        std::vector<char> idText(idSize + 1);
        if (map.Read(map.U32(HALO2_MAP_STRING_INDEX_OFFSET), (char*)idOffsets.data(), idCount * 4) &&
            map.Read(map.U32(HALO2_MAP_STRING_TABLE_OFFSET), idText.data(), idSize)) {
            g_strings.resize(idCount);
            for (uint32_t i = 0; i < idCount; ++i)
                if (idOffsets[i] >= 0 && (uint32_t)idOffsets[i] < idSize) g_strings[i] = idText.data() + idOffsets[i];
        }
    }
    fclose(file);
    if (!ok) {
        LOG("Halo 2: couldn't read tag names from %ls", path.c_str());
        return false;
    }

    g_names.resize(count);
    for (uint32_t i = 0; i < count; ++i)
        if (offsets[i] >= 0 && (uint32_t)offsets[i] < size) g_names[i] = strings.data() + offsets[i];
    LOG("Halo 2: read %u tag names and %zu string ids from %s.map", count, g_strings.size(), mapName.c_str());
    return true;
}

std::vector<Games::TagChoice> Halo2Tags::ListGroup(uint32_t groupMagic) {
    static RawTag raw[1024];
    int n = Collect(groupMagic, raw, _countof(raw));
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

std::string Halo2Tags::StringName(uint32_t id) {
    size_t index = id & 0xFFFF;
    return id && index < g_strings.size() ? g_strings[index] : "";
}

std::string Halo2Tags::FullName(uint32_t datum) {
    if (!GroupOf(datum)) return "";
    size_t index = datum & 0xFFFF;
    return index < g_names.size() ? g_names[index] : "";
}
