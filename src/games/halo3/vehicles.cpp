// Halo 3 vehicles ported from Reach (see NOTES.md, "Reach: piloting Pelicans
// and Phantoms"): "Use AI-only vehicles and seats", the take-pilot-seat key,
// "Upgrade vehicle" and the hold-to-exit for stuck seats. Differences from
// Reach: the seat flag is "Invalid For Player" bit 16, there is no
// unit_enter_vehicle_immediate (seating queues the enter action with the
// "magic" byte itself, see vehicles_offsets.h), and Halo 3 has no Falcon
// (Hornet where a map has one).

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "internal.h"
#include "vehicles_offsets.h"

using namespace Halo3Cheats;

namespace {
    constexpr float kPi = 3.14159265f;
    constexpr ULONGLONG kSeatMs = 3000;       // how long a seat/exit step waits
    constexpr ULONGLONG kMakeMs = 5000;       // how long the upgrade waits for the new vehicle's zone
    constexpr int kRescanTicks = 180;         // seats are rescanned this often while on (zones load tags later)
    constexpr int kMaxVehicleTags = 256;
    constexpr float kUpgradeLift = 3.0f;      // new vehicle above the old one's spot so they don't collide
    constexpr float kTakeSeatReach = 8.0f;    // world units past the aircraft's bounding radius
    constexpr float kTakeSeatCone = 35.0f;    // degrees from the crosshair, on top of its size
    constexpr float kExitHoldSeconds = 0.4f;  // enclosed / taken seats
    constexpr float kStuckExitHoldSeconds = 1.0f;

    using ActionQueueFn = bool(__fastcall*)(uint32_t unit, const void* action);
    using ObjectDetachFn = void(__fastcall*)(uint32_t child);
    using SeatOccupantFn = uint32_t(__fastcall*)(uint32_t vehicle, uint32_t seat);
    using PlacementNewFn = void(__fastcall*)(void* placement, uint32_t tag, uint32_t owner, int64_t);
    using ObjectNewFn = uint32_t(__fastcall*)(void* placement);
    using HeadPositionFn = void(__fastcall*)(uint32_t unit, float* out);
    using SetPositionFn = void(__fastcall*)(uint32_t object, const float* position, const float* forward,
                                            const float* up, const void* location, bool);

    char* g_module = nullptr;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    // ---- Per-map state ---------------------------------------------------

    struct SeatEdit {
        uint32_t vehicle;   // vehi tag datum
        uint32_t* flags;    // the seat's flags in tag memory
        bool driver;
        Restorable<uint32_t> edit;
    };
    std::vector<SeatEdit> g_seats;
    char* g_scnr = nullptr;       // the scenario the list belongs to
    int g_scanIn = 0;             // ticks until the next rescan
    bool g_applied = false;

    bool g_started = false;       // first Tick seen: serials are synced
    uint32_t g_upgradeSerial = 0;
    uint32_t g_takeSerial = 0;
    LARGE_INTEGER g_lastTick{};

    float TickSeconds() {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        float dt = g_lastTick.QuadPart ? (float)(now.QuadPart - g_lastTick.QuadPart) / (float)freq.QuadPart : 0.0f;
        g_lastTick = now;
        return dt > 0.1f ? 0.1f : dt;
    }

    struct RawInstance {
        uint16_t group;
        uint16_t salt;
        uint32_t address;
    };

    // ---- AI-only seats ----------------------------------------------------

    // Every 'vehi' tag in the map (not only the palette), the seats flagged
    // "Invalid For Player", added to g_seats. Tag memory can vanish, so the
    // caller wraps this in SEH.
    void ScanSeats() {
        char* header = *(char**)(g_module + OFFSET_HALO3_TAG_INDEX_HEADER);
        if (!header) return;
        int count = *(int32_t*)(header + HALO3_TAG_HEADER_TAG_COUNT);
        auto instances = *(RawInstance**)(header + HALO3_TAG_HEADER_INSTANCES);
        auto groups = *(uint32_t(**)[4])(header + HALO3_TAG_HEADER_GROUPS);
        if (!instances || !groups) return;
        int found = 0;
        for (int i = 0; i < count && i < 0x10000 && found < kMaxVehicleTags; ++i) {
            const RawInstance& inst = instances[i];
            if (inst.address == 0 || inst.group == 0xFFFF || groups[inst.group][0] != HALO3_TAG_VEHI) continue;
            ++found;
            uint32_t datum = ((uint32_t)inst.salt << 16) | (uint32_t)i;
            char* tag = Halo3Tags::Get(datum);
            int seats = Halo3Tags::BlockCount(tag, HALO3_VEHI_SEATS);
            for (int s = 0; tag && s < seats && s < 64; ++s) {
                char* seat = Halo3Tags::BlockElement(tag, HALO3_VEHI_SEATS, HALO3_VEHI_SEAT_SIZE, s);
                if (!seat) continue;
                uint32_t* flags = &Field<uint32_t>(seat, HALO3_SEAT_FLAGS);
                if (!(*flags & HALO3_SEAT_FLAG_INVALID_FOR_PLAYER)) continue;
                bool known = false;
                for (auto& have : g_seats) known = known || have.flags == flags;
                if (known) continue;
                bool driver = (*flags & HALO3_SEAT_FLAG_DRIVER) != 0;
                g_seats.push_back({datum, flags, driver, {}});
                LOG("Halo 3: vehicle %08X (%s): seat %d%s is AI-only", datum, Halo3Tags::FullName(datum).c_str(), s,
                    driver ? " (driver)" : "");
            }
        }
    }

    void ScanSeatsGuarded() {
        __try {
            ScanSeats();
        } __except (Log::Exception(GetExceptionInformation(), "Halo 3 AI-only seat scan")) {
        }
    }

    void ApplySeatsGuarded(bool on) {
        __try {
            for (auto& seat : g_seats) {
                if (!on) seat.edit.Restore();
                else if (!seat.edit.Ours(seat.flags))
                    seat.edit.Set(seat.flags, *seat.flags & ~HALO3_SEAT_FLAG_INVALID_FOR_PLAYER);
            }
        } __except (Log::Exception(GetExceptionInformation(), "Halo 3 AI-only seats")) {
        }
    }

    void UpdateSeats(bool on) {
        char* scnr = Halo3Tags::GetGlobal(HALO3_TAG_SCNR);
        if (scnr != g_scnr) {  // new map: the old tags' edits went with them
            g_seats.clear();
            g_scnr = scnr;
            g_scanIn = 0;
            g_applied = false;
        }
        if (!on) {
            if (g_applied) ApplySeatsGuarded(false);
            g_applied = false;
            return;
        }
        if (!scnr) return;
        if (g_scanIn-- <= 0) {
            ScanSeatsGuarded();
            g_scanIn = kRescanTicks;
        }
        ApplySeatsGuarded(true);
        g_applied = true;
    }

    // The vehicle's pilot seat can only be taken by AI.
    bool IsAiOnlyVehicle(uint32_t tag) {
        for (auto& seat : g_seats)
            if (seat.driver && seat.vehicle == tag) return true;
        return false;
    }

    // ---- Shared helpers -----------------------------------------------------

    // The vehicle tag's driver seat index (first seat with the Driver flag), or -1.
    int DriverSeat(uint32_t vehicleTag, uint32_t* flagsOut = nullptr) {
        char* tag = Halo3Tags::Get(vehicleTag);
        int seats = Halo3Tags::BlockCount(tag, HALO3_VEHI_SEATS);
        for (int i = 0; tag && i < seats && i < 64; ++i) {
            char* seat = Halo3Tags::BlockElement(tag, HALO3_VEHI_SEATS, HALO3_VEHI_SEAT_SIZE, i);
            if (seat && (Field<uint32_t>(seat, HALO3_SEAT_FLAGS) & HALO3_SEAT_FLAG_DRIVER)) {
                if (flagsOut) *flagsOut = Field<uint32_t>(seat, HALO3_SEAT_FLAGS);
                return i;
            }
        }
        return -1;
    }

    // Queues "enter this seat" for a unit (what unit_enter_vehicle does once
    // its checks pass, minus the animation check). The unit must be on foot.
    bool EnterSeat(uint32_t unit, uint32_t vehicle, int seat) {
        alignas(16) uint8_t action[HALO3_ACTION_STRUCT_SIZE] = {};
        *(uint32_t*)(action + 0) = HALO3_ACTION_ENTER_VEHICLE;
        *(uint32_t*)(action + 4) = vehicle;
        *(int16_t*)(action + HALO3_ACTION_ENTER_VEHICLE_SEAT) = (int16_t)seat;
        *(uint16_t*)(action + HALO3_ACTION_ENTER_VEHICLE_MAGIC) = 0x0100;
        return Engine<ActionQueueFn>(OFFSET_HALO3_UNIT_ACTION_QUEUE_STRUCT)(unit, action);
    }

    uint32_t SeatOccupant(uint32_t vehicle, int seat) {
        return Engine<SeatOccupantFn>(OFFSET_HALO3_SEAT_OCCUPANT)(vehicle, (uint32_t)seat);
    }

    // The top vehicle player 1 is in (a turret's vehicle is attached to the real one), or -1.
    uint32_t PlayerVehicle(const Players& p, Blam::DataArray* objects) {
        if (p.count == 0) return 0xFFFFFFFF;
        uint32_t vehicle = 0xFFFFFFFF;
        uint32_t at = Field<uint32_t>(p.units[0], HALO3_OBJECT_PARENT);
        for (int depth = 0; depth < 4 && at != 0xFFFFFFFF; ++depth) {
            char* object = Blam::ObjectFromDatum(objects, at);
            if (!object || Field<uint8_t>(object, HALO3_OBJECT_TYPE) != HALO3_OBJECT_TYPE_VEHICLE) break;
            vehicle = at;
            at = Field<uint32_t>(object, HALO3_OBJECT_PARENT);
        }
        return vehicle;
    }

    std::string ShortName(uint32_t tag) {
        std::string name = Halo3Tags::FullName(tag);
        return name.substr(name.find_last_of('\\') + 1);
    }

    // A new object of a tag at a position, facing a yaw (spawn.cpp's NewObject).
    uint32_t NewObject(uint32_t tag, const float* position, float yaw) {
        static uint8_t placement[HALO3_PLACEMENT_DATA_SIZE];
        memset(placement, 0, sizeof(placement));
        Engine<PlacementNewFn>(OFFSET_HALO3_OBJECT_PLACEMENT_NEW)(placement, tag, 0xFFFFFFFF, 0);
        memcpy(placement + HALO3_PLACEMENT_POSITION, position, 3 * sizeof(float));
        float* forward = (float*)(placement + HALO3_PLACEMENT_FORWARD);
        forward[0] = cosf(yaw);
        forward[1] = sinf(yaw);
        forward[2] = 0.0f;
        return Engine<ObjectNewFn>(OFFSET_HALO3_OBJECT_NEW)(placement);
    }

    // ---- Take pilot seat ---------------------------------------------------------
    // Pelicans and Phantoms have no boarding seats, so the game can't hijack
    // them. The key picks the AI-only (or otherwise AI-piloted) vehicle nearest
    // the crosshair within reach, deletes its pilot, and once the seat is free
    // seats player 1.

    struct TakeSeat {
        bool active = false;
        uint32_t vehicle = 0xFFFFFFFF;
        int seat = -1;
        bool seating = false;  // the enter is queued
        ULONGLONG deadline = 0;
    };
    TakeSeat g_takeSeat;
    // The last vehicle the key put player 1 in: a scripted vehicle may not let a
    // player out, so the hold-to-exit works there too.
    uint32_t g_tookVehicle = 0xFFFFFFFF;

    bool HasNonPlayerPilot(const Players& p, uint32_t vehicle, uint32_t tag) {
        int seat = DriverSeat(tag);
        if (seat < 0) return false;
        uint32_t pilot = SeatOccupant(vehicle, seat);
        if (pilot == 0xFFFFFFFF) return false;
        for (int i = 0; i < p.count; ++i)
            if (p.datums[i] == pilot) return false;
        return true;
    }

    uint32_t AimedVehicle(void** state, const Players& p, Blam::DataArray* objects) {
        char* control = (char*)state[HALO3_TLS_SLOT_PLAYER_CONTROL];  // entry 0 = local player 1
        if (!control) return 0xFFFFFFFF;
        float yaw = Field<float>(control, HALO3_PLAYER_CONTROL_YAW);
        float pitch = Field<float>(control, HALO3_PLAYER_CONTROL_PITCH);
        float forward[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        float eye[3];
        Engine<HeadPositionFn>(OFFSET_HALO3_UNIT_HEAD_POSITION)(p.datums[0], eye);
        uint32_t best = 0xFFFFFFFF;
        float bestAngle = 1e9f;
        for (int i = 0; i < objects->highWater; ++i) {
            auto entry = (const Blam::ObjectEntry*)objects->Entry(i);
            char* object = entry->object;
            if (!entry->salt || !object || Field<uint8_t>(object, HALO3_OBJECT_TYPE) != HALO3_OBJECT_TYPE_VEHICLE)
                continue;
            if (Field<uint32_t>(object, HALO3_OBJECT_PARENT) != 0xFFFFFFFF) continue;  // turrets on a vehicle
            uint32_t tag = Field<uint32_t>(object, HALO3_OBJECT_TAG_DATUM);
            uint32_t datum = ((uint32_t)entry->salt << 16) | (uint32_t)i;
            if (!IsAiOnlyVehicle(tag) && !HasNonPlayerPilot(p, datum, tag)) continue;
            char* tagData = Halo3Tags::Get(tag);
            if (!tagData) continue;
            float radius = Field<float>(tagData, HALO3_OBJE_BOUNDING_RADIUS);
            if (!(radius > 0.0f && radius < 100.0f)) radius = 3.0f;
            const float* at = &Field<float>(object, HALO3_OBJECT_POSITION);
            float d[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
            float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len > radius + kTakeSeatReach) continue;
            float angle = len < 0.1f ? 0.0f
                                     : acosf(fmaxf(-1.0f, fminf(1.0f, (d[0] * forward[0] + d[1] * forward[1] +
                                                                      d[2] * forward[2]) / len))) * 180.0f / kPi;
            float size = len > radius ? atanf(radius / len) * 180.0f / kPi : 90.0f;  // how big it looks
            if (angle > size + kTakeSeatCone || angle >= bestAngle) continue;
            bestAngle = angle;
            best = datum;
        }
        return best;
    }

    void StartTakeSeat(void** state, const Players& p, Blam::DataArray* objects) {
        g_takeSeat = {};
        if (p.count == 0) return;
        if (PlayerVehicle(p, objects) != 0xFFFFFFFF) { Halo3Internal::Toast("Get out of your vehicle first"); return; }
        if (Field<uint32_t>(p.units[0], HALO3_OBJECT_PARENT) != 0xFFFFFFFF) return;
        uint32_t vehicle = AimedVehicle(state, p, objects);
        char* object = Blam::ObjectFromDatum(objects, vehicle);
        if (!object) { Halo3Internal::Toast("Aim at a piloted or AI-only vehicle up close"); return; }
        uint32_t tag = Field<uint32_t>(object, HALO3_OBJECT_TAG_DATUM);
        TakeSeat& t = g_takeSeat;
        t.seat = DriverSeat(tag);
        if (t.seat < 0) return;
        t.active = true;
        t.vehicle = vehicle;
        t.deadline = GetTickCount64() + kSeatMs;
        uint32_t pilot = SeatOccupant(vehicle, t.seat);
        if (pilot != 0xFFFFFFFF) Halo3Internal::DeleteObject(pilot);
        LOG("Halo 3: take pilot seat of %08X (tag %08X): pilot %08X removed", vehicle, tag, pilot);
    }

    void ContinueTakeSeat(const Players& p, Blam::DataArray* objects) {
        TakeSeat& t = g_takeSeat;
        if (!t.active) return;
        ULONGLONG now = GetTickCount64();
        if (p.count == 0 || !Blam::ObjectFromDatum(objects, t.vehicle)) { t = {}; return; }
        if (!t.seating) {
            if (SeatOccupant(t.vehicle, t.seat) == 0xFFFFFFFF) {
                EnterSeat(p.datums[0], t.vehicle, t.seat);
                t.seating = true;
                t.deadline = now + kSeatMs;
            } else if (now > t.deadline) {
                Halo3Internal::Toast("Couldn't clear the pilot seat");
                t = {};
            }
            return;
        }
        if (PlayerVehicle(p, objects) == t.vehicle) {
            Halo3Internal::Toast("You're the pilot now");
            g_tookVehicle = t.vehicle;
            LOG("Halo 3: take pilot seat: player 1 is piloting %08X", t.vehicle);
            t = {};
        } else if (now > t.deadline) {
            LOG("Halo 3: take pilot seat: player 1 never got into %08X", t.vehicle);
            Halo3Internal::Toast("Couldn't get you into it");
            t = {};
        }
    }

    // ---- Upgrade vehicle ------------------------------------------------------------
    // Banshee > Phantom, Hornet > Pelican (Halo 3 has no Falcon; the Hornet only
    // exists on some maps). Steps, each a tick or more apart: make the new
    // vehicle (loading its zone if needed), detach player 1 from the old one,
    // delete the old one and seat player 1 in the new one's driver seat.

    enum class UpgradeStep { None, Make, Exit, Seat };
    struct Upgrade {
        UpgradeStep step = UpgradeStep::None;
        uint32_t target = 0xFFFFFFFF;  // vehi tag
        uint32_t old = 0xFFFFFFFF;
        uint32_t made = 0xFFFFFFFF;
        int seat = -1;
        float position[3] = {};
        float velocity[3] = {};
        float yaw = 0.0f;
        ULONGLONG deadline = 0;
    };
    Upgrade g_upgrade;

    uint32_t PaletteVehicle(const char* name) {
        char* scnr = Halo3Tags::GetGlobal(HALO3_TAG_SCNR);
        int count = Halo3Tags::BlockCount(scnr, HALO3_SCNR_VEHICLE_PALETTE);
        for (int i = 0; i < count && i < 256; ++i) {
            char* entry = Halo3Tags::BlockElement(scnr, HALO3_SCNR_VEHICLE_PALETTE, HALO3_PALETTE_ENTRY_SIZE, i);
            uint32_t tag = entry ? Field<uint32_t>(entry, HALO3_PALETTE_ENTRY_DATUM) : 0xFFFFFFFF;
            if (tag != 0xFFFFFFFF && ShortName(tag) == name) return tag;
        }
        return 0xFFFFFFFF;
    }

    void StartUpgrade(const Players& p, Blam::DataArray* objects) {
        g_upgrade = {};
        uint32_t vehicle = PlayerVehicle(p, objects);
        char* object = Blam::ObjectFromDatum(objects, vehicle);
        if (!object) { Halo3Internal::Toast("Get in a Banshee or Hornet first"); return; }
        Halo3Tags::LoadNames();
        std::string from = ShortName(Field<uint32_t>(object, HALO3_OBJECT_TAG_DATUM));
        const char* to = from.rfind("banshee", 0) == 0 ? "phantom"
                         : (from.rfind("hornet", 0) == 0 || from.rfind("falcon", 0) == 0) ? "pelican" : nullptr;
        if (!to) { Halo3Internal::Toast("Only a Banshee or Hornet can be upgraded"); return; }
        uint32_t target = PaletteVehicle(to);
        char text[96];
        if (target == 0xFFFFFFFF) {
            sprintf_s(text, "This level has no %s to upgrade to", to);
            Halo3Internal::Toast(text);
            return;
        }
        int seat = DriverSeat(target);
        if (seat < 0) { Halo3Internal::Toast("That vehicle has no pilot seat"); return; }
        Upgrade& u = g_upgrade;
        u.step = UpgradeStep::Make;
        u.target = target;
        u.old = vehicle;
        u.seat = seat;
        u.deadline = GetTickCount64() + kMakeMs;
        memcpy(u.position, &Field<float>(object, HALO3_OBJECT_POSITION), sizeof(u.position));
        memcpy(u.velocity, &Field<float>(object, HALO3_OBJECT_VELOCITY), sizeof(u.velocity));
        const float* forward = &Field<float>(object, HALO3_OBJECT_FORWARD);
        u.yaw = atan2f(forward[1], forward[0]);
        LOG("Halo 3: upgrade %s -> %s (tag %08X)", from.c_str(), to, target);
    }

    void ContinueUpgrade(const Players& p, Blam::DataArray* objects) {
        Upgrade& u = g_upgrade;
        if (u.step == UpgradeStep::None) return;
        ULONGLONG now = GetTickCount64();
        char text[96];
        if (p.count == 0) { u = {}; return; }
        switch (u.step) {
            case UpgradeStep::Make: {
                if (PlayerVehicle(p, objects) != u.old) { u = {}; return; }  // got out meanwhile
                float position[3] = {u.position[0], u.position[1], u.position[2] + kUpgradeLift};
                u.made = Halo3Tags::Get(u.target) ? NewObject(u.target, position, u.yaw) : 0xFFFFFFFF;
                if (u.made != 0xFFFFFFFF) {
                    LOG("Halo 3: upgrade: made %08X; detaching player 1 from %08X", u.made, u.old);
                    Engine<ObjectDetachFn>(OFFSET_HALO3_OBJECT_DETACH)(p.datums[0]);
                    u.step = UpgradeStep::Exit;
                    u.deadline = now + kSeatMs;
                } else {
                    // Not made: its zone may need loading (starts it, true while it is
                    // pending) and its resources a few more frames, so retry until the deadline.
                    Halo3Internal::LoadZoneFor(u.target);
                    if (now > u.deadline) {
                        sprintf_s(text, "Couldn't make a %s here", ShortName(u.target).c_str());
                        Halo3Internal::Toast(text);
                        u = {};
                    }
                }
                return;
            }
            case UpgradeStep::Exit:
                if (Field<uint32_t>(p.units[0], HALO3_OBJECT_PARENT) != 0xFFFFFFFF) {
                    if (now > u.deadline) {
                        LOG("Halo 3: upgrade: player 1 never left %08X", u.old);
                        Halo3Internal::Toast("Couldn't get you out of it");
                        Halo3Internal::DeleteObject(u.made);  // the unused new one
                        u = {};
                    }
                    return;
                }
                Halo3Internal::DeleteObject(u.old);
                if (!Blam::ObjectFromDatum(objects, u.made)) {
                    Halo3Internal::Toast("Couldn't get you into it");
                    u = {};
                    return;
                }
                EnterSeat(p.datums[0], u.made, u.seat);
                LOG("Halo 3: upgrade: removed %08X, seating player 1 in %08X seat %d", u.old, u.made, u.seat);
                u.step = UpgradeStep::Seat;
                u.deadline = now + kSeatMs;
                return;
            case UpgradeStep::Seat:
                if (PlayerVehicle(p, objects) == u.made) {
                    if (char* made = Blam::ObjectFromDatum(objects, u.made))
                        memcpy(&Field<float>(made, HALO3_OBJECT_VELOCITY), u.velocity, sizeof(u.velocity));
                    sprintf_s(text, "Upgraded to %s", ShortName(u.target).c_str());
                    Halo3Internal::Toast(text);
                    LOG("Halo 3: upgrade: player 1 is piloting %08X", u.made);
                    u = {};
                } else if (now > u.deadline) {
                    LOG("Halo 3: upgrade: player 1 never got into %08X (left it there)", u.made);
                    Halo3Internal::Toast("Couldn't get you into it");
                    u = {};
                }
                return;
            default:
                return;
        }
    }

    // ---- Stuck-seat exit (hold X / E) -----------------------------------------------
    // A player can't leave a Phantom's invisible pilot seat (nothing in the
    // game's exit works), nor a scripted vehicle taken with the key. Holding
    // the exit button detaches player 1 (objects_detach's engine call) and
    // either deletes the vehicle (enclosed seat: they'd be inside its hull) or
    // puts them beside it. Only a press made after sitting down counts, so a
    // seat's own exit always gets first go.

    struct HullExit {
        float held = 0.0f;
        uint32_t vehicle = 0xFFFFFFFF;  // detaching from it
        bool remove = false;
        ULONGLONG deadline = 0;
        uint32_t seatedIn = 0xFFFFFFFF;  // player 1's parent when last seen
        bool released = false;           // exit let go since sitting there
    };
    HullExit g_hull;

    // The AI-only vehicle player 1 pilots from an invisible (enclosed) seat, or -1.
    uint32_t EnclosedPilotSeat(const Players& p, Blam::DataArray* objects) {
        uint32_t vehicle = PlayerVehicle(p, objects);
        char* object = Blam::ObjectFromDatum(objects, vehicle);
        uint32_t tag = object ? Field<uint32_t>(object, HALO3_OBJECT_TAG_DATUM) : 0xFFFFFFFF;
        if (!object || !IsAiOnlyVehicle(tag)) return 0xFFFFFFFF;
        uint32_t flags = 0;
        int seat = DriverSeat(tag, &flags);
        if (seat < 0 || !(flags & HALO3_SEAT_FLAG_INVISIBLE)) return 0xFFFFFFFF;
        return SeatOccupant(vehicle, seat) == p.datums[0] ? vehicle : 0xFFFFFFFF;
    }

    void ExitHull(const Players& p, Blam::DataArray* objects, bool on, bool exitHeld, float dt) {
        HullExit& h = g_hull;
        if (h.vehicle != 0xFFFFFFFF) {
            if (p.count > 0 && Field<uint32_t>(p.units[0], HALO3_OBJECT_PARENT) == 0xFFFFFFFF) {
                char* vehicle = Blam::ObjectFromDatum(objects, h.vehicle);
                if (h.remove || !vehicle) {
                    Halo3Internal::DeleteObject(h.vehicle);
                    LOG("Halo 3: player 1 detached from %08X; removed it", h.vehicle);
                } else {
                    // Kept: put them beside it (to its left), clear of the hull.
                    const float* at = &Field<float>(vehicle, HALO3_OBJECT_POSITION);
                    const float* forward = &Field<float>(vehicle, HALO3_OBJECT_FORWARD);
                    char* tag = Halo3Tags::Get(Field<uint32_t>(vehicle, HALO3_OBJECT_TAG_DATUM));
                    float radius = tag ? Field<float>(tag, HALO3_OBJE_BOUNDING_RADIUS) : 3.0f;
                    if (!(radius > 0.0f && radius < 20.0f)) radius = 3.0f;
                    float side = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);
                    float left[2] = {side > 0.01f ? -forward[1] / side : 1.0f, side > 0.01f ? forward[0] / side : 0.0f};
                    float position[3] = {at[0] + left[0] * (radius + 0.5f), at[1] + left[1] * (radius + 0.5f),
                                         at[2] + 0.5f};
                    Engine<SetPositionFn>(OFFSET_HALO3_OBJECT_SET_POSITION)(p.datums[0], position, nullptr, nullptr,
                                                                          nullptr, false);
                    LOG("Halo 3: player 1 detached from %08X; placed beside it", h.vehicle);
                }
                g_tookVehicle = 0xFFFFFFFF;
            } else if (GetTickCount64() < h.deadline) {
                return;
            } else {
                LOG("Halo 3: player 1 still attached to %08X; left it", h.vehicle);
                Halo3Internal::Toast("Couldn't get you out of it");
            }
            h = {};
            return;
        }
        uint32_t vehicle = on && p.count > 0 ? EnclosedPilotSeat(p, objects) : 0xFFFFFFFF;
        bool remove = vehicle != 0xFFFFFFFF;
        uint32_t parent = p.count > 0 ? Field<uint32_t>(p.units[0], HALO3_OBJECT_PARENT) : 0xFFFFFFFF;
        if (parent != h.seatedIn) {
            h.seatedIn = parent;
            h.released = false;
        }
        if (!exitHeld) h.released = true;
        if (!h.released) { h.held = 0.0f; return; }
        float hold = kExitHoldSeconds;
        const char* why = "enclosed pilot seat";
        if (!remove && on && parent != 0xFFFFFFFF) {
            // Any other seat that player 1 is still in: a taken vehicle's quickly,
            // the rest after a second. Placed beside the whole vehicle.
            vehicle = PlayerVehicle(p, objects);
            bool taken = vehicle != 0xFFFFFFFF && vehicle == g_tookVehicle;
            hold = taken ? kExitHoldSeconds : kStuckExitHoldSeconds;
            why = taken ? "taken pilot seat" : "stuck in a seat";
        }
        if (vehicle == 0xFFFFFFFF || !exitHeld) { h.held = 0.0f; return; }
        h.held += dt;
        if (h.held < hold) return;
        LOG("Halo 3: detaching player 1 from %08X (%s)", vehicle, why);
        Engine<ObjectDetachFn>(OFFSET_HALO3_OBJECT_DETACH)(p.datums[0]);
        h = {};
        h.seatedIn = parent;
        h.vehicle = vehicle;
        h.remove = remove;
        h.deadline = GetTickCount64() + kSeatMs;
    }

    // ---- SEH wrappers (plain parameters only, so they can hold __try) ------------------

    void GuardedUpdateSeats(bool on) {
        __try {
            UpdateSeats(on);
        } __except (Log::Exception(GetExceptionInformation(), "Halo 3 AI-only seats update")) {
        }
    }

    void GuardedUpgrade(const Players* p, Blam::DataArray* objects, bool start) {
        __try {
            if (start) StartUpgrade(*p, objects);
            else ContinueUpgrade(*p, objects);
        } __except (Log::Exception(GetExceptionInformation(), "Halo 3 upgrade vehicle")) {
            g_upgrade = {};
        }
    }

    void GuardedTakeSeat(void** state, const Players* p, Blam::DataArray* objects, bool start) {
        __try {
            if (start) StartTakeSeat(state, *p, objects);
            else ContinueTakeSeat(*p, objects);
        } __except (Log::Exception(GetExceptionInformation(), "Halo 3 take pilot seat")) {
            g_takeSeat = {};
        }
    }

    void GuardedExitHull(const Players* p, Blam::DataArray* objects, bool on, bool exitHeld, float dt) {
        __try {
            ExitHull(*p, objects, on, exitHeld, dt);
        } __except (Log::Exception(GetExceptionInformation(), "Halo 3 hull exit")) {
            g_hull = {};
        }
    }

    void ForgetAll() {
        g_upgrade = {};
        g_takeSeat = {};
        g_hull = {};
        g_tookVehicle = 0xFFFFFFFF;
        g_started = false;
    }
}

void Halo3Vehicles::Load(char* module) {
    g_module = module;
    g_seats.clear();
    g_scnr = nullptr;
    g_applied = false;
    ForgetAll();
}

void Halo3Vehicles::Unload() {
    ApplySeatsGuarded(false);  // put every seat flag back
    g_seats.clear();
    g_scnr = nullptr;
    g_applied = false;
    ForgetAll();
    g_module = nullptr;
}

void Halo3Vehicles::Reset() {
    // New map (or unload): the old tags are gone, so there is nothing to restore.
    g_seats.clear();
    g_scnr = nullptr;
    g_applied = false;
    ForgetAll();
}

void Halo3Vehicles::Tick(void** state, const Games::Settings& s, const Games::FrameInput& in, const Players& p,
                         Blam::DataArray* objects) {
    if (!g_module) return;
    float dt = TickSeconds();
    if (!g_started) {  // presses from before now (or a saved settings file's serials) don't count
        g_started = true;
        g_upgradeSerial = s.upgradeVehicleSerial;
        g_takeSerial = s.takeSeatSerial;
    }
    GuardedUpdateSeats(s.pilotAiVehicles);

    if (s.upgradeVehicleSerial != g_upgradeSerial) {
        g_upgradeSerial = s.upgradeVehicleSerial;
        GuardedUpgrade(&p, objects, true);
    }
    if (s.takeSeatSerial != g_takeSerial) {
        g_takeSerial = s.takeSeatSerial;
        if (s.pilotAiVehicles) GuardedTakeSeat(state, &p, objects, true);
    }
    GuardedUpgrade(&p, objects, false);
    GuardedTakeSeat(state, &p, objects, false);
    GuardedExitHull(&p, objects, s.pilotAiVehicles, in.exitHeld, dt);
}
