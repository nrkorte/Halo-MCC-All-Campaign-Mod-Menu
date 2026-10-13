#include "games.h"

#include <mutex>

#include "core/log.h"
#include "halo1/halo1.h"
#include "halo2/halo2.h"
#include "halo3/halo3.h"
#include "halo4/halo4.h"
#include "odst/odst.h"
#include "reach/reach.h"
#include "mcc/mcc.h"

namespace {
    Games::Halo1 g_halo1;
    Games::Halo2 g_halo2;
    Games::Halo3 g_halo3;
    Games::Halo4 g_halo4;
    Games::Odst g_odst;
    Games::Reach g_reach;

    Games::Game* const kGames[] = {&g_halo1, &g_halo2, &g_halo3, &g_halo4, &g_odst, &g_reach};

    // Guards every Game's module/supported state so a title can't unload mid-Tick.
    std::mutex g_mutex;
    Games::Settings g_lastSettings;  // latest menu settings and input, for the main thread
    Games::FrameInput g_lastInput;

    void UnloadGame(Games::Game* game) {
        if (game->supported) game->Unload();
        LOG("%s unloaded", game->Title());
        game->module = nullptr;
        game->supported = false;
    }

    // The title MCC says is being played, if we know it and it's loaded.
    Games::Game* FindRunning() {
        MCC::Running running = MCC::GetRunning();
        if (!running.inGame) return nullptr;

        for (Games::Game* game : kGames)
            if (game->module && game->module == running.module) return game;

        // Module didn't match (e.g. the field moved in an update): fall back to the ID.
        for (Games::Game* game : kGames)
            if (game->module && game->TitleId() == running.titleId) return game;

        return nullptr;
    }
}

void Games::OnModuleLoad(HMODULE module) {
    for (Game* game : kGames) {
        if (GetModuleHandleW(game->Dll()) != module) continue;

        std::lock_guard<std::mutex> lock(g_mutex);

        // Loaded again without us seeing the unload: clean up the old hooks first.
        if (game->module && game->module != module) UnloadGame(game);
        if (game->module == module) return;

        game->module = module;
        game->supported = game->Load(module);
        LOG("%s loaded at %p (%s)", game->Title(), (void*)module,
            game->supported ? "supported" : "not supported yet");
        return;
    }
    LOG("Unknown module loaded at %p", (void*)module);
}

void Games::OnModuleUnload(HMODULE module) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Game* game : kGames)
        if (game->module == module) UnloadGame(game);
}

const char* Games::RunningTitle() {
    std::lock_guard<std::mutex> lock(g_mutex);
    Game* game = FindRunning();
    return game ? game->Title() : nullptr;
}

Games::Status Games::Tick(const Settings& settings, const FrameInput& input) {
    std::lock_guard<std::mutex> lock(g_mutex);

    g_lastSettings = settings;
    g_lastInput = input;

    Status status;
    Game* game = FindRunning();
    if (!game) return status;

    status.title = game->Title();
    status.supported = game->supported;
    if (!game->supported) return status;

    status.features = game->Features();
    status.features2 = game->Features2();
    game->Tick(settings, input, status);
    return status;
}

void Games::MainThreadTick() {
    Game* game;
    Settings settings;
    FrameInput input;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        game = FindRunning();
        if (!game || !game->supported) return;
        settings = g_lastSettings;
        input = g_lastInput;
    }
    // Outside the lock: engine calls made here must never wait on the render
    // thread's Tick. The title can't unload mid-call, since we're on its
    // main thread inside its own input polling.
    game->MainThreadTick(settings, input);
}
