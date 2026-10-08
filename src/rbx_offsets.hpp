// Roblox client offsets shared by every tool in src/.
// Live-verified for version-02c37bc51a384b8f; re-dump with rbx_dump.exe after a Roblox update
// and change them here only.
#pragma once
#include <cstdint>

namespace off {
    constexpr uintptr_t VE_POINTER         = 0x858d208;  // VisualEngine::Pointer (RVA from module base)
    constexpr uintptr_t VE_FAKE_DM         = 0xaf0;      // VisualEngine::FakeDataModel
    constexpr uintptr_t FAKE_REAL_DM       = 0x1f8;      // FakeDataModel::RealDataModel
    constexpr uintptr_t DM_PLACE_ID        = 0x188;      // DataModel::PlaceId
    constexpr uintptr_t DM_WORKSPACE       = 0x150;      // DataModel::Workspace
    constexpr uintptr_t DM_GAME_LOADED     = 0x5d0;      // DataModel::GameLoaded
    constexpr uintptr_t INST_NAME          = 0x70;       // NameContainer: pointer to string struct
    constexpr uintptr_t INST_CHILDREN      = 0x78;       // ChildrenStart: pointer to {begin,end}
    constexpr uintptr_t INST_CLASS_DESC    = 0x18;       // ClassDescriptor
    constexpr uintptr_t INST_PARENT        = 0x68;
    constexpr uintptr_t PLAYERS_LOCAL      = 0x120;      // Players::LocalPlayer
    constexpr uintptr_t PLAYER_USERID      = 0xc0;       // Player::UserId (int64)
    constexpr uintptr_t VALUE              = 0xa8;       // ValueBase::Value
    constexpr uintptr_t SCREEN_GUI_ENABLED = 0x4b4;      // ScreenGui::Enabled (byte)
    constexpr uintptr_t GUI_VISIBLE        = 0x59d;      // GuiObject::Visible (byte)
    constexpr uintptr_t GUI_TEXT           = 0xdf0;      // GuiObject::Text (inline string: len@+0x10, cap@+0x18)
    constexpr uintptr_t GUI_IMAGE          = 0xc10;      // GuiObject::Image on ImageButton
}
