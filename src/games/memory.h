#pragma once

// Helpers for editing game memory, shared by every title's cheats. Everything
// here touches raw game memory, so callers must be inside SEH.

namespace Games {
    template <typename T>
    T& Field(char* base, int offset) { return *(T*)(base + offset); }

    // A value we've overwritten, remembered so it can be restored. If the
    // value no longer matches what we wrote, the game replaced it (new map,
    // checkpoint), so the current value becomes the new original.
    template <typename T>
    struct Restorable {
        T* addr = nullptr;
        T original{};
        T written{};

        bool Ours(T* at) const { return addr == at && *at == written; }

        void Set(T* at, T value) {
            if (!Ours(at)) { addr = at; original = *at; }
            *at = value;
            written = value;
        }

        void Restore() {
            if (addr && *addr == written) *addr = original;
            addr = nullptr;
        }
    };

    // Scales a float by `m`, or restores it when m == 1.
    inline void Scale(Restorable<float>& p, float* at, float m) {
        if (m == 1.0f) {
            if (p.addr == at) p.Restore();
            else p.addr = nullptr;  // data moved (new map): nothing of ours to restore
            return;
        }
        float base = p.Ours(at) ? p.original : *at;
        p.Set(at, base * m);
        p.original = base;
    }

    // A field in a tag block that has at most one element per tag (the
    // vehicle physics types: only the vehicle's own type is filled in).
    struct BlockField {
        int block;        // tag block offset in the tag
        int elementSize;
        int offset;       // field offset in the element
    };

    // Vehicle speed: scales fields of the tag of the vehicle player 1 is in,
    // and puts them back when they get out or switch vehicles. `fields` are
    // the tag's own fields, `blockFields` fields in its physics-type blocks;
    // `element(tag, block, size)` returns a block's first element or nullptr.
    template <int N>
    struct VehicleScaler {
        char* tag = nullptr;
        Restorable<float> values[N];

        template <typename ElementFn>
        void Update(char* vehicleTag, const int* fields, int fieldCount, const BlockField* blockFields,
                    int blockFieldCount, float m, ElementFn element) {
            if (m == 1.0f) vehicleTag = nullptr;
            if (vehicleTag != tag) {
                for (auto& v : values) v.Restore();
                tag = vehicleTag;
            }
            if (!tag) return;
            int n = 0;
            for (int i = 0; i < fieldCount && n < N; ++i, ++n) Scale(values[n], &Field<float>(tag, fields[i]), m);
            for (int i = 0; i < blockFieldCount && n < N; ++i, ++n) {
                char* e = element(tag, blockFields[i].block, blockFields[i].elementSize);
                if (e) Scale(values[n], &Field<float>(e, blockFields[i].offset), m);
            }
        }

        void Forget() {
            tag = nullptr;
            for (auto& v : values) v = {};
        }
    };

    // Vehicle god mode: the vehicles the players sit in, and what those are
    // mounted on (a turret's vehicle), are made invulnerable; the ones they
    // leave are put back, unless they were invulnerable already (scripted).
    // `vehicle(datum)` returns a vehicle object or nullptr, `parent(object)`
    // its parent's datum, `has(object)` whether it's invulnerable, and
    // `set(object, on)` sets or clears that (on is called every tick, so it
    // can also top up health).
    struct VehicleGodState {
        static constexpr int kMax = 8;
        static constexpr int kMaxOurs = 32;
        uint32_t datums[kMax] = {};
        bool already[kMax] = {};  // invulnerable before we touched it
        int count = 0;
        // Every vehicle we turned the flag on for (not ones that had it).
        // A checkpoint saved while we held the flag brings it back when it's
        // loaded, after we'd let go of the vehicle; without this list the
        // flag then looked like the game's own and stayed on for good (a
        // Halo 4 Wraith that rockets couldn't hurt, 2026-10-02).
        uint32_t ours[kMaxOurs] = {};
        int oursNext = 0;

        bool Ours(uint32_t d) const {
            for (uint32_t o : ours)
                if (o == d && d) return true;
            return false;
        }

        template <typename VehicleFn, typename ParentFn, typename HasFn, typename SetFn>
        void Update(const uint32_t* seats, int seatCount, bool on, VehicleFn vehicle, ParentFn parent, HasFn has,
                    SetFn set) {
            uint32_t now[kMax];
            bool nowAlready[kMax];
            int n = 0;
            for (int i = 0; on && i < seatCount; ++i) {
                uint32_t d = seats[i];
                for (int depth = 0; depth < 4 && n < kMax; ++depth) {
                    char* object = vehicle(d);
                    if (!object) break;
                    bool seen = false;
                    for (int k = 0; k < n; ++k) seen |= now[k] == d;
                    if (!seen) {
                        int old = -1;
                        for (int k = 0; k < count; ++k)
                            if (datums[k] == d) old = k;
                        nowAlready[n] = old >= 0 ? already[old] : has(object) && !Ours(d);
                        now[n++] = d;
                    }
                    d = parent(object);
                }
            }
            for (int i = 0; i < count; ++i) {
                bool kept = false;
                for (int k = 0; k < n; ++k) kept |= now[k] == datums[i];
                if (char* object = kept || already[i] ? nullptr : vehicle(datums[i])) set(object, false);
            }
            // Ours that came back (checkpoint loads) while nobody is in them.
            for (uint32_t& d : ours) {
                if (!d) continue;
                bool kept = false;
                for (int k = 0; k < n; ++k) kept |= now[k] == d;
                if (kept) continue;
                char* object = vehicle(d);
                if (object && has(object)) set(object, false);
            }
            for (int k = 0; k < n; ++k) {
                char* object = vehicle(now[k]);
                if (!object) continue;
                set(object, true);
                if (!nowAlready[k] && !Ours(now[k])) {
                    ours[oursNext] = now[k];
                    oursNext = (oursNext + 1) % kMaxOurs;
                }
            }
            for (int k = 0; k < n; ++k) {
                datums[k] = now[k];
                already[k] = nowAlready[k];
            }
            count = n;
        }

        void Forget() {
            count = 0;
            for (uint32_t& d : ours) d = 0;
            oursNext = 0;
        }
    };

    // A fixed value for a float field in a vehicle's tag.
    struct TagValue {
        int offset;
        float value;
    };

    // Sets fields in the tag of the vehicle player 1 is in, and puts them
    // back when they get out, switch vehicles or turn it off.
    template <int N>
    struct VehicleSetter {
        char* tag = nullptr;
        Restorable<float> values[N];

        void Update(char* vehicleTag, const TagValue* fields, int count, bool on) {
            if (!on) vehicleTag = nullptr;
            if (vehicleTag != tag) {
                for (auto& v : values) v.Restore();
                tag = vehicleTag;
            }
            for (int i = 0; tag && i < count && i < N; ++i) values[i].Set(&Field<float>(tag, fields[i].offset), fields[i].value);
        }

        void Forget() {
            tag = nullptr;
            for (auto& v : values) v = {};
        }
    };
}
