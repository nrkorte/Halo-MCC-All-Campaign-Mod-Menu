#pragma once

#include "games/game.h"

namespace Games {
    class Halo2 : public Game {
    public:
        Halo2() : Game("Halo 2", L"halo2.dll", 1) {}

        bool Load(HMODULE module) override;
        void Unload() override;
        uint64_t Features() const override;
        uint64_t Features2() const override;
        void Tick(const Settings& settings, const FrameInput& input, Status& status) override;

    private:
        void* m_createProjectiles = nullptr;
        void* m_gameTick = nullptr;
    };
}
