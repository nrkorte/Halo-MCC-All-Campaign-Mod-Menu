#pragma once

#include "games/game.h"

namespace Games {
    class Reach : public Game {
    public:
        Reach() : Game("Halo: Reach", L"haloreach.dll", 6) {}

        bool Load(HMODULE module) override;
        void Unload() override;
        uint64_t Features() const override;
        uint64_t Features2() const override;
        void Tick(const Settings& settings, const FrameInput& input, Status& status) override;

    private:
        void* m_hooks[9] = {};  // hooked addresses, removed on unload
    };
}
