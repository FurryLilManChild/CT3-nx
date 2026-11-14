#include "app/app.h"
#include "core/input.h"
#include "gpu/backend.h"

namespace xdk {
extern uint32_t g_music_pct, g_effects_pct;  // dsound.cpp
}

namespace ct3 {
// the Switch port menu's settings (switch/menu.cpp), kept in the settings file
uint32_t g_button_map = 0;  // 0: as buttons_by_name; else 8 nibbles, each a Switch button slot + 1
bool g_invert_x = false, g_invert_y = false, g_fps_counter = false;
uint32_t g_stick_response = 0;  // left stick left/right: 0 linear, 1 gentle, 2 gentler (Mark, 2026-11-13: Sonic felt touchy)
void ScriptPad(int port, input::Pad& pad);  // pad_script.cpp
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Crazy Taxi 3",              // window and splash
    .short_title = L"Crazy Taxi 3",        // message boxes
    .name = L"CT3",                 // window classes, crash dumps, handover event
    .log = "ct3",                   // log file prefix
    .settings = "ct3-settings.toml",  // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "CT3"}},  // each recompiled xbe and its exe, for title launches
    .options = {{"button_map", nullptr, &ct3::g_button_map},
                {"invert_camera_x", &ct3::g_invert_x, nullptr},
                {"invert_camera_y", &ct3::g_invert_y, nullptr},
                {"fps_counter", &ct3::g_fps_counter, nullptr},
                {"stick_response", nullptr, &ct3::g_stick_response},
                {"music_volume", nullptr, &xdk::g_music_pct},
                {"effects_volume", nullptr, &xdk::g_effects_pct}},  // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = nullptr,                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = ct3::ScriptPad,          // scripted test presses (pad_script.cpp), none unless asked for
    .rumble = nullptr,                    // changes a rumble before it is set
};
