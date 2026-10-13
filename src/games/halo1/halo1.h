#pragma once

#include "games/game.h"

namespace Games {
    class Halo1 : public Game {
    public:
        Halo1() : Game("Halo: CE", L"halo1.dll", 0) {}

        bool Load(HMODULE module) override;
        void Unload() override;
        uint64_t Features() const override;
        uint64_t Features2() const override;
        void Tick(const Settings& settings, const FrameInput& input, Status& status) override;

    private:
        void* m_gameTick = nullptr;
    };
}
