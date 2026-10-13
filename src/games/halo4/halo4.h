#pragma once

#include "games/game.h"

namespace Games {
    class Halo4 : public Game {
    public:
        Halo4() : Game("Halo 4", L"halo4.dll", 3) {}

        bool Load(HMODULE module) override;
        void Unload() override;
        uint64_t Features() const override;
        uint64_t Features2() const override;
        void Tick(const Settings& settings, const FrameInput& input, Status& status) override;

    private:
        void* m_hooks[6] = {};  // hooked addresses, removed on unload
    };
}
