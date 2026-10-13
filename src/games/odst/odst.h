#pragma once

#include "games/game.h"

namespace Games {
    class Odst : public Game {
    public:
        Odst() : Game("Halo 3: ODST", L"halo3odst.dll", 5) {}

        bool Load(HMODULE module) override;
        void Unload() override;
        uint64_t Features() const override;
        uint64_t Features2() const override;
        void Tick(const Settings& settings, const FrameInput& input, Status& status) override;
        void MainThreadTick(const Settings& settings, const FrameInput& input) override;

    private:
        void* m_engineTarget = nullptr;
        void* m_frameInputTarget = nullptr;
        void* m_cameraUpdateTarget = nullptr;
        void* m_unitActionTarget = nullptr;
        void* m_barrelFireTarget = nullptr;
    };
}
