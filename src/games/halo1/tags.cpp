#include "tags.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "offsets.h"

namespace {
    char* g_base = nullptr;  // halo1.dll

    char* Entries() { return *(char**)(g_base + OFFSET_HALO1_TAG_INSTANCES); }

    int TagCount() {
        char* header = *(char**)(g_base + OFFSET_HALO1_TAG_HEADER);
        return header ? *(int32_t*)(header + HALO1_TAG_HEADER_COUNT) : 0;
    }

    char* Entry(int index) {
        char* entries = Entries();
        return entries && index >= 0 && index < TagCount() ? entries + index * HALO1_TAG_ENTRY_SIZE : nullptr;
    }
}

void Halo1Tags::SetModule(HMODULE halo1) {
    g_base = (char*)halo1;
}

char* Halo1Tags::Resolve(int32_t address) {
    if (!g_base || !address) return nullptr;
    int64_t virtualBase = *(int64_t*)(g_base + OFFSET_HALO1_TAG_VIRTUAL);
    char* realBase = *(char**)(g_base + OFFSET_HALO1_TAG_REAL);
    return realBase ? realBase + ((int64_t)address - virtualBase) : nullptr;
}

char* Halo1Tags::Get(uint32_t datum) {
    if (!g_base || datum == 0xFFFFFFFF) return nullptr;
    char* entry = Entry((int16_t)(datum & 0xFFFF));
    if (!entry || *(uint32_t*)(entry + HALO1_TAG_ENTRY_DATUM) != datum) return nullptr;
    return Resolve(*(int32_t*)(entry + HALO1_TAG_ENTRY_DATA));
}

uint32_t Halo1Tags::GroupOf(uint32_t datum) {
    if (!g_base || datum == 0xFFFFFFFF) return 0;
    char* entry = Entry((int16_t)(datum & 0xFFFF));
    if (!entry || *(uint32_t*)(entry + HALO1_TAG_ENTRY_DATUM) != datum) return 0;
    return *(uint32_t*)(entry + HALO1_TAG_ENTRY_GROUP);
}

char* Halo1Tags::Scenario() { return g_base ? *(char**)(g_base + OFFSET_HALO1_SCENARIO) : nullptr; }

char* Halo1Tags::First(uint32_t groupMagic) {
    if (!g_base) return nullptr;
    int count = TagCount();
    for (int i = 0; i < count; ++i) {
        char* entry = Entry(i);
        if (entry && *(uint32_t*)(entry + HALO1_TAG_ENTRY_GROUP) == groupMagic)
            return Resolve(*(int32_t*)(entry + HALO1_TAG_ENTRY_DATA));
    }
    return nullptr;
}

int Halo1Tags::BlockCount(char* tag, int blockOffset) {
    return tag ? *(int32_t*)(tag + blockOffset) : 0;
}

char* Halo1Tags::BlockElement(char* tag, int blockOffset, int elementSize, int index) {
    if (!tag || index < 0 || index >= BlockCount(tag, blockOffset)) return nullptr;
    char* elements = Resolve(*(int32_t*)(tag + blockOffset + 4));
    return elements ? elements + (int64_t)elementSize * index : nullptr;
}

namespace {
    struct RawTag {
        uint32_t datum;
        char name[256];
    };

    // Copies matching tags' datums and names under SEH (tag memory can
    // vanish mid-read during a map change). Returns how many were found.
    int Collect(uint32_t groupMagic, RawTag* out, int max) {
        __try {
            int count = TagCount();
            int n = 0;
            for (int i = 0; i < count && i < 0x10000 && n < max; ++i) {
                char* entry = Entry(i);
                if (!entry || *(uint32_t*)(entry + HALO1_TAG_ENTRY_GROUP) != groupMagic) continue;
                const char* path = Halo1Tags::Resolve(*(int32_t*)(entry + HALO1_TAG_ENTRY_NAME));
                out[n].datum = *(uint32_t*)(entry + HALO1_TAG_ENTRY_DATUM);
                out[n].name[0] = 0;
                if (path) strncpy_s(out[n].name, path, _TRUNCATE);
                ++n;
            }
            return n;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }
}

std::vector<Games::TagChoice> Halo1Tags::ListGroup(uint32_t groupMagic) {
    static RawTag raw[1024];
    std::vector<Games::TagChoice> out;
    if (!g_base) return out;
    int n = Collect(groupMagic, raw, _countof(raw));
    for (int i = 0; i < n; ++i) {
        std::string name = raw[i].name;
        size_t slash = name.find_last_of('\\');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (name.empty()) name = "tag " + std::to_string(raw[i].datum & 0xFFFF);
        out.push_back({raw[i].datum, name});
    }
    std::sort(out.begin(), out.end(),
              [](const Games::TagChoice& a, const Games::TagChoice& b) { return a.name < b.name; });
    return out;
}
