#include "gb/app/frontend/gba_realtime.hpp"

#ifdef GBEMU_USE_SDL2
#include "gb/app/frontend/debug_ui.hpp"
#include "gb/app/frontend/realtime/top_menu.hpp"
#include "gb/app/frontend/realtime_support.hpp"
#include "gb/app/sdl_compat.hpp"
#include "gb/core/environment.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace gb::frontend {

namespace {

constexpr int kPanelWidth = 268;
constexpr int kGbaCyclesPerFrame = 280896;
constexpr int kGbaCpuHz = 16777216;

enum class GbaDebugEditField {
    None,
    Address,
    Value,
};

struct GbaDebugEditState {
    GbaDebugEditField field = GbaDebugEditField::None;
    std::string text{};
};

bool isHexKeyChar(char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

char keyToHexChar(SDL_Keycode key) {
    if (key >= SDLK_0 && key <= SDLK_9) {
        return static_cast<char>('0' + (key - SDLK_0));
    }
    if (key >= SDLK_a && key <= SDLK_f) {
        return static_cast<char>('A' + (key - SDLK_a));
    }
    switch (key) {
    case SDLK_KP_0: return '0';
    case SDLK_KP_1: return '1';
    case SDLK_KP_2: return '2';
    case SDLK_KP_3: return '3';
    case SDLK_KP_4: return '4';
    case SDLK_KP_5: return '5';
    case SDLK_KP_6: return '6';
    case SDLK_KP_7: return '7';
    case SDLK_KP_8: return '8';
    case SDLK_KP_9: return '9';
    default: break;
    }
    return '\0';
}

std::optional<u32> parseHexU32(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    u32 out = 0;
    for (const char ch : text) {
        if (!isHexKeyChar(ch)) {
            return std::nullopt;
        }
        const unsigned value = ch >= '0' && ch <= '9'
            ? static_cast<unsigned>(ch - '0')
            : static_cast<unsigned>(std::toupper(static_cast<unsigned char>(ch)) - 'A' + 10);
        out = static_cast<u32>((out << 4U) | value);
    }
    return out;
}

std::string hex8(u8 value) {
    char out[3]{};
    std::snprintf(out, sizeof(out), "%02X", static_cast<unsigned>(value));
    return out;
}

std::string hex32(u32 value) {
    char out[9]{};
    std::snprintf(out, sizeof(out), "%08X", static_cast<unsigned>(value));
    return out;
}

std::string hex16(u16 value) {
    char out[5]{};
    std::snprintf(out, sizeof(out), "%04X", static_cast<unsigned>(value));
    return out;
}

bool audioDisabledByDebug() {
    return gb::environmentVariableEnabled("GBEMU_GBA_DISABLE_AUDIO");
}

SDL_Rect computeGbaDestinationRect(
    int outputW,
    int outputH,
    int srcW,
    int srcH,
    bool showPanel,
    bool showTopMenuBar,
    bool fullscreen,
    FullscreenScaleMode fullscreenMode
) {
    const int top = showTopMenuBar ? topMenuBarHeight() : 0;
    const int contentW = std::max(1, outputW - (showPanel ? kPanelWidth : 0));
    const int contentH = std::max(1, outputH - top);
    if (fullscreen && fullscreenMode != FullscreenScaleMode::CrispFit) {
        return SDL_Rect{0, top, contentW, contentH};
    }

    const int scaleX = std::max(1, contentW / srcW);
    const int scaleY = std::max(1, contentH / srcH);
    const int scale = std::max(1, std::min(scaleX, scaleY));
    const int dstW = srcW * scale;
    const int dstH = srcH * scale;
    return SDL_Rect{(contentW - dstW) / 2, top + (contentH - dstH) / 2, dstW, dstH};
}

void setGbaButtonFromKey(gba::InputState& input, SDL_Keycode key, bool pressed) {
    switch (key) {
    case SDLK_RIGHT:
    case SDLK_d: input.right = pressed; break;
    case SDLK_LEFT:
    case SDLK_a: input.left = pressed; break;
    case SDLK_UP:
    case SDLK_w: input.up = pressed; break;
    case SDLK_DOWN:
    case SDLK_s: input.down = pressed; break;
    case SDLK_z:
    case SDLK_j:
    case SDLK_k:
    case SDLK_c: input.a = pressed; break;
    case SDLK_x:
    case SDLK_u:
    case SDLK_i:
    case SDLK_v: input.b = pressed; break;
    case SDLK_BACKSPACE:
    case SDLK_RSHIFT: input.select = pressed; break;
    case SDLK_RETURN:
    case SDLK_SPACE: input.start = pressed; break;
    case SDLK_q: input.l = pressed; break;
    case SDLK_e: input.r = pressed; break;
    default: break;
    }
}

gba::InputState readGbaInput(const gba::InputState& eventInput, SDL_GameController* gamepad) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    gba::InputState input = eventInput;
    input.right = input.right || keys[SDL_SCANCODE_RIGHT] != 0 || keys[SDL_SCANCODE_D] != 0;
    input.left = input.left || keys[SDL_SCANCODE_LEFT] != 0 || keys[SDL_SCANCODE_A] != 0;
    input.up = input.up || keys[SDL_SCANCODE_UP] != 0 || keys[SDL_SCANCODE_W] != 0;
    input.down = input.down || keys[SDL_SCANCODE_DOWN] != 0 || keys[SDL_SCANCODE_S] != 0;
    input.a = input.a || keys[SDL_SCANCODE_Z] != 0 || keys[SDL_SCANCODE_J] != 0 || keys[SDL_SCANCODE_K] != 0 || keys[SDL_SCANCODE_C] != 0;
    input.b = input.b || keys[SDL_SCANCODE_X] != 0 || keys[SDL_SCANCODE_U] != 0 || keys[SDL_SCANCODE_I] != 0 || keys[SDL_SCANCODE_V] != 0;
    input.select = input.select || keys[SDL_SCANCODE_BACKSPACE] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0;
    input.start = input.start || keys[SDL_SCANCODE_RETURN] != 0 || keys[SDL_SCANCODE_SPACE] != 0;
    input.l = input.l || keys[SDL_SCANCODE_Q] != 0;
    input.r = input.r || keys[SDL_SCANCODE_E] != 0;
    if (gamepad != nullptr) {
        input.a = input.a || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_A) != 0;
        input.b = input.b || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_B) != 0;
        input.select = input.select || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_BACK) != 0;
        input.start = input.start || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_START) != 0;
        input.up = input.up || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0;
        input.down = input.down || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0;
        input.left = input.left || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0;
        input.right = input.right || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0;
        input.l = input.l || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
        input.r = input.r || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
    }
    return input;
}

std::uint8_t expand5(std::uint16_t v) {
    return static_cast<std::uint8_t>((v << 3U) | (v >> 2U));
}

std::uint8_t expand6(std::uint16_t v) {
    return static_cast<std::uint8_t>((v << 2U) | (v >> 4U));
}

template <typename Core>
bool saveGbaFramePpm(const std::string& path, const Core& core) {
    std::filesystem::path outPath(path);
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "P6\n" << Core::ScreenWidth << " " << Core::ScreenHeight << "\n255\n";
    for (const u16 pixel : core.framebuffer()) {
        const std::uint8_t r = expand5(static_cast<std::uint16_t>((pixel >> 11U) & 0x1FU));
        const std::uint8_t g = expand6(static_cast<std::uint16_t>((pixel >> 5U) & 0x3FU));
        const std::uint8_t b = expand5(static_cast<std::uint16_t>(pixel & 0x1FU));
        const char bytes[3] = {
            static_cast<char>(r),
            static_cast<char>(g),
            static_cast<char>(b),
        };
        out.write(bytes, 3);
    }
    return static_cast<bool>(out);
}

template <typename Core>
void rgb565FramebufferToRgb24(const Core& core, std::vector<unsigned char>& out) {
    out.resize(static_cast<std::size_t>(Core::ScreenWidth) * static_cast<std::size_t>(Core::ScreenHeight) * 3U);
    const auto& frame = core.framebuffer();
    for (std::size_t i = 0; i < frame.size(); ++i) {
        const u16 pixel = frame[i];
        out[i * 3U + 0U] = expand5(static_cast<std::uint16_t>((pixel >> 11U) & 0x1FU));
        out[i * 3U + 1U] = expand6(static_cast<std::uint16_t>((pixel >> 5U) & 0x3FU));
        out[i * 3U + 2U] = expand5(static_cast<std::uint16_t>(pixel & 0x1FU));
    }
}

template <typename Core>
std::vector<std::string> buildGbaMemoryRows(const Core& core, u32 startAddress, int rows) {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(std::max(0, rows)));
    for (int row = 0; row < rows; ++row) {
        const u32 base = startAddress + static_cast<u32>(row * 8);
        std::string line = hex32(base) + " ";
        for (int i = 0; i < 8; ++i) {
            if (const auto value = core.debugRead8(base + static_cast<u32>(i))) {
                line += hex8(*value);
            } else {
                line += "__";
            }
            if (i != 7) {
                line += " ";
            }
        }
        out.push_back(line);
    }
    return out;
}

std::string gbaRegisterName(unsigned reg) {
    switch (reg) {
    case 13: return "sp";
    case 14: return "lr";
    case 15: return "pc";
    default: return "r" + std::to_string(reg);
    }
}

std::string decodeThumbInstruction(u16 op, u32 pc) {
    if (op == 0x46C0U) {
        return "nop";
    }
    if ((op & 0xF800U) == 0x2000U) {
        return "mov " + gbaRegisterName((op >> 8U) & 0x7U) + ", #" + std::to_string(op & 0xFFU);
    }
    if ((op & 0xF800U) == 0x2800U) {
        return "cmp " + gbaRegisterName((op >> 8U) & 0x7U) + ", #" + std::to_string(op & 0xFFU);
    }
    if ((op & 0xF800U) == 0x3000U) {
        return "add " + gbaRegisterName((op >> 8U) & 0x7U) + ", #" + std::to_string(op & 0xFFU);
    }
    if ((op & 0xF800U) == 0x3800U) {
        return "sub " + gbaRegisterName((op >> 8U) & 0x7U) + ", #" + std::to_string(op & 0xFFU);
    }
    if ((op & 0xF800U) == 0x4800U) {
        const u32 target = ((pc + 4U) & ~3U) + static_cast<u32>(op & 0xFFU) * 4U;
        return "ldr " + gbaRegisterName((op >> 8U) & 0x7U) + ", [pc,#" + std::to_string((op & 0xFFU) * 4U) + "] ; " + hex32(target);
    }
    if ((op & 0xF800U) == 0x6000U) {
        return "str " + gbaRegisterName(op & 0x7U) + ", [" + gbaRegisterName((op >> 3U) & 0x7U) + ",#" + std::to_string(((op >> 6U) & 0x1FU) * 4U) + "]";
    }
    if ((op & 0xF800U) == 0x6800U) {
        return "ldr " + gbaRegisterName(op & 0x7U) + ", [" + gbaRegisterName((op >> 3U) & 0x7U) + ",#" + std::to_string(((op >> 6U) & 0x1FU) * 4U) + "]";
    }
    if ((op & 0xFF00U) == 0xB500U) {
        return "push {...}" + std::string((op & 0x0100U) ? ",lr" : "");
    }
    if ((op & 0xFF00U) == 0xBD00U) {
        return "pop {...}" + std::string((op & 0x0100U) ? ",pc" : "");
    }
    if ((op & 0xFF87U) == 0x4700U) {
        return "bx " + gbaRegisterName((op >> 3U) & 0xFU);
    }
    if ((op & 0xF800U) == 0xE000U) {
        const int offset = static_cast<int>((op & 0x07FFU) << 21U) >> 20;
        return "b " + hex32(static_cast<u32>(static_cast<int>(pc + 4U) + offset));
    }
    if ((op & 0xFF00U) == 0xDF00U) {
        return "swi #" + hex8(static_cast<u8>(op & 0xFFU));
    }
    return ".hword " + hex16(op);
}

std::string armConditionSuffix(u32 cond) {
    static const char* names[16] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "", "nv"};
    return names[cond & 0xFU];
}

std::string decodeArmInstruction(u32 op, u32 pc) {
    const std::string cond = armConditionSuffix(op >> 28U);
    if (op == 0xE1A00000U) {
        return "nop";
    }
    if ((op & 0x0FFFFFF0U) == 0x012FFF10U) {
        return "bx" + cond + " " + gbaRegisterName(op & 0xFU);
    }
    if ((op & 0x0F000000U) == 0x0A000000U) {
        const int offset = static_cast<int>((op & 0x00FFFFFFU) << 8U) >> 6;
        const std::string mnemonic = (op & 0x01000000U) != 0 ? "bl" : "b";
        return mnemonic + cond + " " + hex32(static_cast<u32>(static_cast<int>(pc + 8U) + offset));
    }
    if ((op & 0x0F000000U) == 0x0F000000U) {
        return "swi" + cond + " #" + hex32(op & 0x00FFFFFFU);
    }
    if ((op & 0x0C000000U) == 0x00000000U) {
        const unsigned opcode = (op >> 21U) & 0xFU;
        const bool immediate = (op & (1U << 25U)) != 0;
        const unsigned rn = (op >> 16U) & 0xFU;
        const unsigned rd = (op >> 12U) & 0xFU;
        const u32 operand = immediate ? (op & 0xFFU) : (op & 0xFU);
        const std::string rhs = immediate ? ("#" + std::to_string(operand)) : gbaRegisterName(operand);
        switch (opcode) {
        case 0x2: return "sub" + cond + " " + gbaRegisterName(rd) + ", " + gbaRegisterName(rn) + ", " + rhs;
        case 0x4: return "add" + cond + " " + gbaRegisterName(rd) + ", " + gbaRegisterName(rn) + ", " + rhs;
        case 0xA: return "cmp" + cond + " " + gbaRegisterName(rn) + ", " + rhs;
        case 0xD: return "mov" + cond + " " + gbaRegisterName(rd) + ", " + rhs;
        default: break;
        }
    }
    if ((op & 0x0C000000U) == 0x04000000U) {
        const bool load = (op & (1U << 20U)) != 0;
        const unsigned rn = (op >> 16U) & 0xFU;
        const unsigned rd = (op >> 12U) & 0xFU;
        const u32 offset = op & 0xFFFU;
        return std::string(load ? "ldr" : "str") + cond + " " + gbaRegisterName(rd) + ", [" + gbaRegisterName(rn) + ",#" + std::to_string(offset) + "]";
    }
    return ".word " + hex32(op);
}

template <typename Core>
std::vector<std::string> buildGbaDisasmRows(const Core& core, u32 pc, bool thumb, int rows) {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(std::max(0, rows)));
    const u32 step = thumb ? 2U : 4U;
    const u32 start = pc >= step * 2U ? pc - step * 2U : pc;
    for (int i = 0; i < rows; ++i) {
        const u32 addr = start + static_cast<u32>(i) * step;
        const bool current = (addr & ~(step - 1U)) == (pc & ~(step - 1U));
        std::string line = current ? "> " : "  ";
        line += hex32(addr) + " ";
        if (thumb) {
            if (const auto op = core.debugRead16(addr)) {
                line += hex16(*op) + " " + decodeThumbInstruction(*op, addr);
            } else {
                line += "____ ???";
            }
        } else {
            if (const auto op = core.debugRead32(addr)) {
                line += hex32(*op) + " " + decodeArmInstruction(*op, addr);
            } else {
                line += "________ ???";
            }
        }
        out.push_back(line);
    }
    return out;
}

void drawTopMenuBar(
    SDL_Renderer* renderer,
    int outputW,
    std::optional<TopMenuSection> openSection,
    std::optional<TopMenuSection> hoveredSection,
    int hoveredItem
) {
    SDL_SetRenderDrawColor(renderer, 12, 15, 24, 238);
    SDL_Rect bar{0, 0, outputW, topMenuBarHeight()};
    SDL_RenderFillRect(renderer, &bar);
    SDL_SetRenderDrawColor(renderer, 50, 58, 78, 255);
    SDL_RenderDrawRect(renderer, &bar);

    for (int i = 0; i < static_cast<int>(TopMenuSection::Count); ++i) {
        const auto section = static_cast<TopMenuSection>(i);
        const auto rect = topMenuSectionRect(outputW, section);
        const bool active = openSection == section || hoveredSection == section;
        SDL_SetRenderDrawColor(renderer, active ? 55 : 22, active ? 68 : 28, active ? 96 : 44, 255);
        SDL_Rect r{rect.x, rect.y, rect.w, rect.h};
        SDL_RenderFillRect(renderer, &r);
        drawHexText(renderer, rect.x + 6, rect.y + 5, topMenuSectionLabel(section), SDL_Color{230, 236, 255, 255}, 1);
    }

    if (!openSection.has_value()) {
        return;
    }

    const auto section = openSection.value();
    const auto drop = topMenuDropdownRect(outputW, section);
    SDL_SetRenderDrawColor(renderer, 18, 22, 34, 248);
    SDL_Rect d{drop.x, drop.y, drop.w, drop.h};
    SDL_RenderFillRect(renderer, &d);
    SDL_SetRenderDrawColor(renderer, 74, 86, 116, 255);
    SDL_RenderDrawRect(renderer, &d);

    const auto& items = topMenuItems(section);
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const int y = drop.y + 4 + i * topMenuItemHeight();
        if (i == hoveredItem) {
            SDL_SetRenderDrawColor(renderer, 54, 64, 92, 255);
            SDL_Rect row{drop.x + 3, y - 1, drop.w - 6, topMenuItemHeight()};
            SDL_RenderFillRect(renderer, &row);
        }
        drawHexText(renderer, drop.x + 8, y + 3, items[static_cast<std::size_t>(i)].label, SDL_Color{226, 232, 248, 255}, 1);
    }
}

void drawGbaDebugPanel(
    SDL_Renderer* renderer,
    int outputW,
    int outputH,
    const std::string& title,
    const std::string& coreName,
    const std::string& message,
    const gba::GbaDebugSnapshot& snapshot,
    const std::vector<std::string>& disasmRows,
    const std::vector<std::string>& memoryRows,
    u32 debugAddress,
    std::size_t debugRegionIndex,
    const GbaDebugEditState& editState,
    const std::vector<u32>& breakpoints,
    bool paused,
    bool muted,
    bool fastForward,
    double fps,
    bool showBreakpointMenu,
    bool showSearchPanel
) {
    const int x = outputW - kPanelWidth;
    SDL_SetRenderDrawColor(renderer, 10, 12, 20, 238);
    SDL_Rect panel{x, 0, kPanelWidth, outputH};
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 58, 70, 98, 255);
    SDL_RenderDrawRect(renderer, &panel);

    const SDL_Color active{226, 236, 255, 255};
    const SDL_Color dim{138, 152, 182, 255};
    const SDL_Color warn{255, 222, 128, 255};
    const SDL_Color ok{134, 222, 170, 255};

    int y = 12;
    drawHexText(renderer, x + 12, y, "GBA DEBUG", SDL_Color{176, 208, 246, 255}, 2);
    y += 24;
    drawHexText(renderer, x + 12, y, paused ? "PAUSED" : "RUNNING", paused ? warn : ok, 1);
    drawHexText(renderer, x + 94, y, muted ? "MUTED" : "AUDIO ON", muted ? warn : ok, 1);
    drawHexText(renderer, x + 178, y, fastForward ? "FF" : "1X", fastForward ? warn : active, 1);
    y += 18;
    drawHexText(renderer, x + 12, y, "CORE " + coreName.substr(0, 28), active, 1);
    y += 14;
    drawHexText(renderer, x + 12, y, "ROM  " + title.substr(0, 28), active, 1);
    y += 14;
    drawHexText(renderer, x + 12, y, "FPS  " + std::to_string(static_cast<int>(fps + 0.5)), fps >= 55.0 ? ok : warn, 1);
    y += 14;
    drawHexText(renderer, x + 12, y, "FRAME " + std::to_string(snapshot.available ? snapshot.frameCounter : 0U), active, 1);
    y += 22;

    drawHexText(renderer, x + 12, y, "SESSION", SDL_Color{176, 208, 246, 255}, 1);
    y += 14;
    drawHexText(renderer, x + 18, y, "SPACE PAUSE  P/M MUTE  TAB FF", dim, 1);
    y += 12;
    drawHexText(renderer, x + 18, y, "F FULL  N SCALE  F9/F12 CAP", dim, 1);
    y += 12;
    drawHexText(renderer, x + 18, y, "I/F1 DEBUG  F3/F10 TOP  F11 HELP", dim, 1);
    y += 22;

    drawHexText(renderer, x + 12, y, "CPU ARM7TDMI", SDL_Color{176, 208, 246, 255}, 1);
    y += 14;
    if (snapshot.available) {
        drawHexText(renderer, x + 18, y, "PC " + hex32(snapshot.cpu.regs[15]) + " " + (snapshot.cpu.thumb ? "THUMB" : "ARM"), active, 1);
        y += 12;
        drawHexText(renderer, x + 18, y, "SP " + hex32(snapshot.cpu.regs[13]) + " LR " + hex32(snapshot.cpu.regs[14]).substr(0, 4), active, 1);
        y += 12;
        drawHexText(renderer, x + 18, y, "CPSR " + hex32(snapshot.cpu.cpsr) + " " + snapshot.cpu.mode, active, 1);
        y += 12;
        drawHexText(renderer, x + 18, y, "R0 " + hex32(snapshot.cpu.regs[0]) + " R1 " + hex32(snapshot.cpu.regs[1]).substr(0, 4), dim, 1);
        y += 12;
        drawHexText(renderer, x + 18, y, "R2 " + hex32(snapshot.cpu.regs[2]) + " R3 " + hex32(snapshot.cpu.regs[3]).substr(0, 4), dim, 1);
    } else {
        drawHexText(renderer, x + 18, y, "DEBUG PROFUNDO INDISPONIVEL", SDL_Color{255, 170, 150, 255}, 1);
        y += 12;
        drawHexText(renderer, x + 18, y, "USE CORE NATIVO MGBA", SDL_Color{255, 170, 150, 255}, 1);
    }
    y += 22;

    if (!disasmRows.empty()) {
        drawHexText(renderer, x + 12, y, "DISASM", SDL_Color{176, 208, 246, 255}, 1);
        y += 14;
        for (const auto& row : disasmRows) {
            if (y > outputH - 190) {
                break;
            }
            const bool current = !row.empty() && row[0] == '>';
            drawHexText(renderer, x + 18, y, row.substr(0, 38), current ? warn : dim, 1);
            y += 12;
        }
        y += 10;
    }

    std::string regionLabel = "REGION";
    if (snapshot.available && debugRegionIndex < snapshot.memoryBlocks.size()) {
        regionLabel = snapshot.memoryBlocks[debugRegionIndex].shortName;
    }
    drawHexText(renderer, x + 12, y, "MEM " + hex32(debugAddress) + " " + regionLabel, SDL_Color{176, 208, 246, 255}, 1);
    y += 14;
    for (const auto& row : memoryRows) {
        if (y > outputH - 110) {
            break;
        }
        drawHexText(renderer, x + 18, y, row, active, 1);
        y += 12;
    }
    y += 10;

    if (editState.field != GbaDebugEditField::None) {
        const std::string prefix = editState.field == GbaDebugEditField::Address ? "EDIT ADDR " : "EDIT VAL  ";
        drawHexText(renderer, x + 18, y, prefix + editState.text + "_", warn, 1);
        y += 14;
    } else {
        drawHexText(renderer, x + 18, y, "1-9 REGION  G ADDR  E VALUE", dim, 1);
        y += 12;
        drawHexText(renderer, x + 18, y, "PGUP/DN SCROLL  INS INC BYTE", dim, 1);
        y += 14;
    }

    drawHexText(renderer, x + 12, y, "BREAKPOINTS", SDL_Color{176, 208, 246, 255}, 1);
    y += 14;
    drawHexText(renderer, x + 18, y, showBreakpointMenu ? "BP PANEL ON" : "BP PANEL OFF", showBreakpointMenu ? warn : dim, 1);
    y += 12;
    drawHexText(renderer, x + 18, y, showSearchPanel ? "SEARCH PANEL ON" : "SEARCH PANEL OFF", showSearchPanel ? warn : dim, 1);
    y += 12;
    const int visibleBp = std::min<int>(3, static_cast<int>(breakpoints.size()));
    for (int i = 0; i < visibleBp; ++i) {
        drawHexText(renderer, x + 18, y, "BP" + std::to_string(i + 1) + " " + hex32(breakpoints[static_cast<std::size_t>(i)]), warn, 1);
        y += 12;
    }
    if (breakpoints.empty()) {
        drawHexText(renderer, x + 18, y, "B TOGGLE BP AT PC", dim, 1);
        y += 12;
    }

    if (snapshot.available && !snapshot.memoryBlocks.empty() && y < outputH - 44) {
        const auto& block = snapshot.memoryBlocks.front();
        drawHexText(renderer, x + 12, y + 6, "MAP " + block.shortName + " " + hex32(block.start), dim, 1);
    }

    if (!message.empty()) {
        drawHexText(renderer, x + 12, std::max(12, outputH - 22), message.substr(0, 34), warn, 1);
    }
}

void drawGbaControlsOverlay(SDL_Renderer* renderer, int outputW, int outputH) {
    const int w = std::min(430, std::max(260, outputW - 60));
    const int h = 150;
    const int x = (outputW - w) / 2;
    const int y = (outputH - h) / 2;
    SDL_SetRenderDrawColor(renderer, 14, 18, 28, 246);
    SDL_Rect box{x, y, w, h};
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 88, 104, 140, 255);
    SDL_RenderDrawRect(renderer, &box);
    drawHexText(renderer, x + 14, y + 12, "CONTROLES GBA", SDL_Color{236, 242, 255, 255}, 1);
    drawHexText(renderer, x + 14, y + 34, "D PAD  SETAS OU WASD", SDL_Color{190, 206, 232, 255}, 1);
    drawHexText(renderer, x + 14, y + 50, "A      Z J K C", SDL_Color{190, 206, 232, 255}, 1);
    drawHexText(renderer, x + 14, y + 66, "B      X U I V", SDL_Color{190, 206, 232, 255}, 1);
    drawHexText(renderer, x + 14, y + 82, "L/R    Q / E", SDL_Color{190, 206, 232, 255}, 1);
    drawHexText(renderer, x + 14, y + 98, "START  ENTER OU SPACE", SDL_Color{190, 206, 232, 255}, 1);
    drawHexText(renderer, x + 14, y + 114, "SELECT BACKSPACE RSHIFT", SDL_Color{190, 206, 232, 255}, 1);
    drawHexText(renderer, x + 14, y + 132, "F11 FECHA", SDL_Color{255, 222, 128, 255}, 1);
}

template <typename Core>
int runGbaRealtimeCommon(
    Core& core,
    int scale,
    const std::string& windowTitle,
    const std::string& statePath,
    const std::string& captureDir
) {
    if (!core.loaded()) {
        return 1;
    }

    const bool debugDisableAudio = audioDisabledByDebug();
    const Uint32 sdlInitFlags = SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER
        | (debugDisableAudio ? 0U : static_cast<Uint32>(SDL_INIT_AUDIO));
    if (SDL_Init(sdlInitFlags) != 0) {
        return 1;
    }

    const int baseScale = std::max(1, scale);
    const int windowW = Core::ScreenWidth * baseScale;
    const int windowH = Core::ScreenHeight * baseScale;
    SDL_Window* window = SDL_CreateWindow(
        windowTitle.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        windowW,
        windowH,
        SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING,
        Core::ScreenWidth,
        Core::ScreenHeight
    );
    if (!texture) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_Texture* upscaleTexture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        Core::ScreenWidth,
        Core::ScreenHeight
    );
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    if (upscaleTexture != nullptr) {
        SDL_SetTextureScaleMode(upscaleTexture, SDL_ScaleModeLinear);
    }
#endif
    std::vector<unsigned char> upscalePixels;
    std::vector<unsigned char> sharpPixels;

    SDL_GameController* gamepad = nullptr;
    for (int i = 0, count = SDL_NumJoysticks(); i < count; ++i) {
        if (SDL_IsGameController(i)) {
            gamepad = SDL_GameControllerOpen(i);
            if (gamepad != nullptr) {
                break;
            }
        }
    }

    SDL_AudioSpec have{};
    SDL_AudioDeviceID audioDev = 0;
    SDL_AudioStream* audioStream = nullptr;
    bool audioStarted = false;
    if (!debugDisableAudio) {
        SDL_AudioSpec want{};
        want.freq = Core::SampleRate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = nullptr;
        audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
        if (audioDev != 0) {
            audioStream = SDL_NewAudioStream(AUDIO_S16SYS, 2, Core::SampleRate, have.format, have.channels, have.freq);
        }
        if (audioDev != 0 && audioStream != nullptr) {
            SDL_PauseAudioDevice(audioDev, 1);
        } else if (audioDev != 0) {
            SDL_CloseAudioDevice(audioDev);
            audioDev = 0;
        }
    }
    const bool audioReady = audioDev != 0 && audioStream != nullptr;
    const Uint32 bytesPerSecond = audioReady ? static_cast<Uint32>(have.freq * have.channels * static_cast<int>(sizeof(int16_t))) : 0U;
    const Uint32 minQueuedAudioBytes = audioReady ? bytesPerSecond / 12U : 0U;
    const Uint32 maxQueuedAudioBytes = audioReady ? bytesPerSecond / 5U : 0U;

    constexpr auto kGbaFrameBudget = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(static_cast<double>(kGbaCyclesPerFrame) / static_cast<double>(kGbaCpuHz)));
#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    bool running = true;
    bool backToMenu = false;
    bool paused = false;
    bool muted = false;
    bool fastForward = false;
    bool fullscreen = false;
    bool showPanel = false;
    bool showTopMenuBar = true;
    bool showControlsMenu = false;
    bool showScaleMenu = false;
    bool showBreakpointMenu = false;
    bool showSearchPanel = false;
    FullscreenScaleMode fullscreenMode = FullscreenScaleMode::CrispFit;
    int scaleMenuIndex = 0;
    std::optional<TopMenuSection> openTopMenuSection{};
    std::optional<TopMenuSection> hoveredTopMenuSection{};
    int hoveredTopMenuItem = -1;
    std::string uiMessage{};
    int uiMessageFrames = 0;
    gba::InputState eventInput{};
    auto nextFrame = std::chrono::steady_clock::now();
    auto fpsWindowStart = nextFrame;
    int fpsFrames = 0;
    double fps = 0.0;
    u32 debugAddress = 0x02000000U;
    std::size_t debugRegionIndex = 1;
    GbaDebugEditState debugEdit{};
    std::vector<u32> debugBreakpoints{};

    const auto setMessage = [&](std::string message, int frames = 120) {
        uiMessage = std::move(message);
        uiMessageFrames = frames;
    };

    const auto clearAudio = [&]() {
        if (audioReady) {
            SDL_ClearQueuedAudio(audioDev);
            SDL_AudioStreamClear(audioStream);
            audioStarted = false;
        }
    };

    const auto toggleFullscreen = [&]() {
        fullscreen = !fullscreen;
        SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        if (!fullscreen) {
            showScaleMenu = false;
        }
        setMessage(fullscreen ? "FULLSCREEN ON" : "FULLSCREEN OFF");
    };

    const auto captureFrame = [&]() {
        const std::string path = nextCapturePath(captureDir);
        if (saveGbaFramePpm(path, core)) {
            std::cout << "capture GBA salva: " << path << "\n";
            setMessage("CAPTURE OK");
        } else {
            setMessage("CAPTURE FAIL");
        }
    };

    const auto toggleBreakpointAtPc = [&]() {
        const auto snapshot = core.debugSnapshot();
        if (!snapshot.available) {
            setMessage("BP REQUER MGBA NATIVO");
            return;
        }
        const u32 pc = snapshot.cpu.regs[15] & (snapshot.cpu.thumb ? ~1U : ~3U);
        const auto it = std::find(debugBreakpoints.begin(), debugBreakpoints.end(), pc);
        if (it != debugBreakpoints.end()) {
            debugBreakpoints.erase(it);
            setMessage("BP OFF " + hex32(pc));
            return;
        }
        if (debugBreakpoints.size() >= 16U) {
            setMessage("BP LIST FULL");
            return;
        }
        debugBreakpoints.push_back(pc);
        std::sort(debugBreakpoints.begin(), debugBreakpoints.end());
        setMessage("BP ON " + hex32(pc));
    };

    const auto syncDebugAddressToRegion = [&](const gba::GbaDebugSnapshot& snapshot) {
        if (!snapshot.available || debugRegionIndex >= snapshot.memoryBlocks.size()) {
            return;
        }
        debugAddress = snapshot.memoryBlocks[debugRegionIndex].start;
        setMessage("REGION " + snapshot.memoryBlocks[debugRegionIndex].shortName + " " + hex32(debugAddress), 75);
    };

    const auto beginAddressEdit = [&]() {
        debugEdit.field = GbaDebugEditField::Address;
        debugEdit.text = hex32(debugAddress);
        showPanel = true;
    };

    const auto beginValueEdit = [&]() {
        debugEdit.field = GbaDebugEditField::Value;
        debugEdit.text = hex8(core.debugRead8(debugAddress).value_or(0U));
        showPanel = true;
    };

    const auto cancelDebugEdit = [&]() {
        debugEdit = GbaDebugEditState{};
    };

    const auto applyDebugEdit = [&]() {
        const auto parsed = parseHexU32(debugEdit.text);
        if (!parsed.has_value()) {
            setMessage("HEX INVALIDO");
            cancelDebugEdit();
            return;
        }
        if (debugEdit.field == GbaDebugEditField::Address) {
            debugAddress = *parsed;
            setMessage("MEM " + hex32(debugAddress));
        } else if (debugEdit.field == GbaDebugEditField::Value) {
            const u8 value = static_cast<u8>(*parsed & 0xFFU);
            setMessage(core.debugWrite8(debugAddress, value) ? "WRITE " + hex32(debugAddress) + "=" + hex8(value) : "MEM WRITE FAIL");
        }
        cancelDebugEdit();
    };

    const auto dispatchTopMenuAction = [&](TopMenuAction action) {
        switch (action) {
        case TopMenuAction::TogglePause:
            paused = !paused;
            clearAudio();
            setMessage(paused ? "PAUSE ON" : "PAUSE OFF");
            break;
        case TopMenuAction::ToggleMute:
            muted = !muted;
            clearAudio();
            setMessage(muted ? "MUTE ON" : "MUTE OFF");
            break;
        case TopMenuAction::ToggleFastForward:
            fastForward = !fastForward;
            clearAudio();
            setMessage(fastForward ? "FF ON" : "FF OFF");
            break;
        case TopMenuAction::SaveState:
            setMessage(core.saveStateToFile(statePath) ? "SAVE STATE OK" : "SAVE STATE FAIL");
            break;
        case TopMenuAction::LoadState:
            setMessage(core.loadStateFromFile(statePath) ? "LOAD STATE OK" : "LOAD STATE FAIL");
            clearAudio();
            break;
        case TopMenuAction::BackToMenu:
            backToMenu = true;
            running = false;
            break;
        case TopMenuAction::ExitApp:
            running = false;
            break;
        case TopMenuAction::ToggleFullscreen:
            toggleFullscreen();
            break;
        case TopMenuAction::ToggleScaleMenu:
            if (!fullscreen) {
                setMessage("SCALE ONLY FULLSCREEN");
                break;
            }
            showScaleMenu = !showScaleMenu;
            showControlsMenu = false;
            scaleMenuIndex = static_cast<int>(fullscreenMode);
            break;
        case TopMenuAction::CaptureFrame:
            captureFrame();
            break;
        case TopMenuAction::ToggleDebugPanel:
            showPanel = !showPanel;
            setMessage(showPanel ? "DEBUG ON" : "DEBUG OFF");
            break;
        case TopMenuAction::ToggleBreakpointMenu:
            showPanel = true;
            showBreakpointMenu = !showBreakpointMenu;
            setMessage(showBreakpointMenu ? "BP MENU ON" : "BP MENU OFF");
            break;
        case TopMenuAction::ToggleSearchPanel:
            showPanel = true;
            showSearchPanel = !showSearchPanel;
            setMessage(showSearchPanel ? "SEARCH ON" : "SEARCH OFF");
            break;
        case TopMenuAction::OpenControlsMenu:
            showControlsMenu = !showControlsMenu;
            break;
        case TopMenuAction::TogglePaletteMenu:
        case TopMenuAction::CycleFilter:
        case TopMenuAction::ToggleRunLabMcpBridge:
        case TopMenuAction::RunLabExportProfile:
        case TopMenuAction::CycleLinkMode:
        case TopMenuAction::NetplayDelayDown:
        case TopMenuAction::NetplayDelayUp:
        default:
            setMessage("NAO DISPONIVEL NO GBA");
            break;
        }
    };

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                eventInput = gba::InputState{};
                continue;
            }
            if (event.type == SDL_MOUSEMOTION) {
                hoveredTopMenuSection.reset();
                hoveredTopMenuItem = -1;
                if (showTopMenuBar) {
                    const int mx = event.motion.x;
                    const int my = event.motion.y;
                    hoveredTopMenuSection = hitTestTopMenuSection(0, mx, my);
                    if (openTopMenuSection.has_value()) {
                        const auto drop = topMenuDropdownRect(0, openTopMenuSection.value());
                        if (topMenuRectContains(drop, mx, my)) {
                            const int localY = my - drop.y - 4;
                            if (localY >= 0) {
                                hoveredTopMenuItem = localY / topMenuItemHeight();
                            }
                        }
                    }
                }
                continue;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                const int mx = event.button.x;
                const int my = event.button.y;
                if (showScaleMenu) {
                    int outputW = 0;
                    int outputH = 0;
                    SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
                    const auto layout = fullscreenScaleMenuLayout(outputW, outputH);
                    if (popupLayoutHitClose(layout, mx, my)) {
                        showScaleMenu = false;
                        setMessage("SCALE MENU OFF");
                        continue;
                    }
                    for (int i = 0; i < 3; ++i) {
                        const SDL_Rect row{layout.box.x + 8, layout.box.y + 26 + i * 24, layout.box.w - 16, 18};
                        if (mx >= row.x && mx < row.x + row.w && my >= row.y && my < row.y + row.h) {
                            fullscreenMode = static_cast<FullscreenScaleMode>(i);
                            scaleMenuIndex = i;
                            showScaleMenu = false;
                            setMessage(std::string("SCALE ") + scaleModeUiName(fullscreenMode));
                            break;
                        }
                    }
                    continue;
                }
                if (!showTopMenuBar) {
                    continue;
                }
                if (const auto section = hitTestTopMenuSection(0, mx, my); section.has_value()) {
                    if (openTopMenuSection == section) {
                        openTopMenuSection.reset();
                    } else {
                        openTopMenuSection = section;
                    }
                    continue;
                }
                if (openTopMenuSection.has_value()) {
                    if (const auto action = hitTestTopMenuAction(0, openTopMenuSection.value(), mx, my); action.has_value()) {
                        dispatchTopMenuAction(action.value());
                    }
                    openTopMenuSection.reset();
                    continue;
                }
            }
            if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (debugEdit.field != GbaDebugEditField::None) {
                    if (key == SDLK_ESCAPE) {
                        cancelDebugEdit();
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        applyDebugEdit();
                    } else if (key == SDLK_BACKSPACE) {
                        if (!debugEdit.text.empty()) {
                            debugEdit.text.pop_back();
                        }
                    } else if (const char hex = keyToHexChar(key); hex != '\0') {
                        const std::size_t maxLen = debugEdit.field == GbaDebugEditField::Address ? 8U : 2U;
                        if (debugEdit.text.size() < maxLen) {
                            debugEdit.text.push_back(hex);
                        }
                    }
                    continue;
                }
                if (showControlsMenu) {
                    if (key == SDLK_ESCAPE || key == SDLK_F11) {
                        showControlsMenu = false;
                        setMessage("CONTROLS MENU OFF");
                    }
                    continue;
                }
                if (showScaleMenu) {
                    if (key == SDLK_UP || key == SDLK_LEFT) {
                        scaleMenuIndex = (scaleMenuIndex + 2) % 3;
                    } else if (key == SDLK_DOWN || key == SDLK_RIGHT) {
                        scaleMenuIndex = (scaleMenuIndex + 1) % 3;
                    } else if (key == SDLK_1) {
                        scaleMenuIndex = 0;
                    } else if (key == SDLK_2) {
                        scaleMenuIndex = 1;
                    } else if (key == SDLK_3) {
                        scaleMenuIndex = 2;
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        fullscreenMode = static_cast<FullscreenScaleMode>(scaleMenuIndex);
                        showScaleMenu = false;
                        setMessage(std::string("SCALE ") + scaleModeUiName(fullscreenMode));
                    } else if (key == SDLK_ESCAPE || key == SDLK_n) {
                        showScaleMenu = false;
                    }
                    continue;
                }
                if (event.key.repeat != 0) {
                    continue;
                }
                if (key == SDLK_ESCAPE) {
                    if (openTopMenuSection.has_value()) {
                        openTopMenuSection.reset();
                    } else if (showControlsMenu) {
                        showControlsMenu = false;
                    } else {
                        running = false;
                    }
                } else if (key == SDLK_SPACE) {
                    dispatchTopMenuAction(TopMenuAction::TogglePause);
                } else if (key == SDLK_p || key == SDLK_m) {
                    dispatchTopMenuAction(TopMenuAction::ToggleMute);
                } else if (key == SDLK_f) {
                    dispatchTopMenuAction(TopMenuAction::ToggleFullscreen);
                } else if (key == SDLK_n) {
                    dispatchTopMenuAction(TopMenuAction::ToggleScaleMenu);
                } else if (key == SDLK_F1 || key == SDLK_i) {
                    dispatchTopMenuAction(TopMenuAction::ToggleDebugPanel);
                } else if (showPanel && key == SDLK_F7) {
                    paused = true;
                    core.setInputState(readGbaInput(eventInput, gamepad));
                    core.stepInstruction();
                    clearAudio();
                    setMessage(core.debugAvailable() ? "STEP INSTRUCTION" : "STEP INDISPONIVEL");
                } else if (showPanel && key == SDLK_b) {
                    toggleBreakpointAtPc();
                } else if (showPanel && key == SDLK_d) {
                    dispatchTopMenuAction(TopMenuAction::ToggleBreakpointMenu);
                } else if (showPanel && key == SDLK_s) {
                    dispatchTopMenuAction(TopMenuAction::ToggleSearchPanel);
                } else if (showPanel && key == SDLK_g) {
                    beginAddressEdit();
                } else if (showPanel && key == SDLK_e) {
                    beginValueEdit();
                } else if (showPanel && key >= SDLK_1 && key <= SDLK_9) {
                    const auto snapshot = core.debugSnapshot();
                    const std::size_t index = static_cast<std::size_t>(key - SDLK_1);
                    if (snapshot.available && index < snapshot.memoryBlocks.size()) {
                        debugRegionIndex = index;
                        syncDebugAddressToRegion(snapshot);
                    }
                } else if (showPanel && key == SDLK_PAGEUP) {
                    debugAddress -= 0x100U;
                    setMessage("MEM " + hex32(debugAddress), 45);
                } else if (showPanel && key == SDLK_PAGEDOWN) {
                    debugAddress += 0x100U;
                    setMessage("MEM " + hex32(debugAddress), 45);
                } else if (showPanel && key == SDLK_HOME) {
                    const auto snapshot = core.debugSnapshot();
                    if (snapshot.available) {
                        debugAddress = snapshot.cpu.regs[15] & ~0xFU;
                        setMessage("MEM PC " + hex32(debugAddress), 60);
                    }
                } else if (showPanel && key == SDLK_INSERT) {
                    if (core.debugWrite8(debugAddress, static_cast<u8>((core.debugRead8(debugAddress).value_or(0U) + 1U) & 0xFFU))) {
                        setMessage("MEM INC " + hex32(debugAddress), 45);
                    } else {
                        setMessage("MEM WRITE FAIL");
                    }
                } else if (key == SDLK_F3 || key == SDLK_F10) {
                    showTopMenuBar = !showTopMenuBar;
                    openTopMenuSection.reset();
                    setMessage(showTopMenuBar ? "TOP BAR ON" : "TOP BAR OFF");
                } else if (key == SDLK_F11) {
                    dispatchTopMenuAction(TopMenuAction::OpenControlsMenu);
                } else if (key == SDLK_F9 || key == SDLK_F12) {
                    captureFrame();
                } else if (key == SDLK_TAB) {
                    fastForward = true;
                    clearAudio();
                    setMessage("FAST FORWARD", 45);
                } else {
                    setGbaButtonFromKey(eventInput, key, true);
                }
                continue;
            }
            if (event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_TAB) {
                    fastForward = false;
                    clearAudio();
                } else {
                    setGbaButtonFromKey(eventInput, event.key.keysym.sym, false);
                }
                continue;
            }
        }

        if (!paused) {
            core.setInputState(readGbaInput(eventInput, gamepad));
            core.runFrame();
            if (!debugBreakpoints.empty()) {
                const auto snapshot = core.debugSnapshot();
                if (snapshot.available) {
                    const u32 pc = snapshot.cpu.regs[15] & (snapshot.cpu.thumb ? ~1U : ~3U);
                    if (std::find(debugBreakpoints.begin(), debugBreakpoints.end(), pc) != debugBreakpoints.end()) {
                        paused = true;
                        clearAudio();
                        setMessage("BREAKPOINT " + hex32(pc), 180);
                    }
                }
            }
        } else {
            core.setInputState(gba::InputState{});
        }

        auto samples = core.takeSamples();
        if (audioReady && !samples.empty() && !muted && !paused && !fastForward) {
            const int inputBytes = static_cast<int>(samples.size() * sizeof(int16_t));
            if (SDL_AudioStreamPut(audioStream, samples.data(), inputBytes) == 0) {
                const int availableBytes = SDL_AudioStreamAvailable(audioStream);
                const Uint32 queued = SDL_GetQueuedAudioSize(audioDev);
                const Uint32 budget = queued >= maxQueuedAudioBytes ? 0U : maxQueuedAudioBytes - queued;
                int bytesToPull = std::min(availableBytes, static_cast<int>(budget));
                const int bytesPerFrame = static_cast<int>(have.channels) * static_cast<int>(sizeof(int16_t));
                if (bytesPerFrame > 1) {
                    bytesToPull -= bytesToPull % bytesPerFrame;
                }
                if (bytesToPull > 0) {
                    std::vector<std::uint8_t> converted(static_cast<std::size_t>(bytesToPull));
                    const int got = SDL_AudioStreamGet(audioStream, converted.data(), bytesToPull);
                    if (got > 0) {
                        SDL_QueueAudio(audioDev, converted.data(), static_cast<Uint32>(got));
                    }
                }
            }
            if (!audioStarted && SDL_GetQueuedAudioSize(audioDev) >= minQueuedAudioBytes) {
                SDL_PauseAudioDevice(audioDev, 0);
                audioStarted = true;
            }
        } else if (audioReady && (muted || paused || fastForward)) {
            clearAudio();
        }

        int outputW = windowW;
        int outputH = windowH;
        SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
        const SDL_Rect dst = computeGbaDestinationRect(outputW, outputH, Core::ScreenWidth, Core::ScreenHeight, showPanel, showTopMenuBar, fullscreen, fullscreenMode);
        SDL_Texture* renderTexture = texture;
        if (fullscreen && !showPanel && fullscreenMode == FullscreenScaleMode::FullStretchSharp && upscaleTexture != nullptr) {
            rgb565FramebufferToRgb24(core, upscalePixels);
            sharpPixels = upscalePixels;
            applySharpenRgb24(upscalePixels.data(), sharpPixels.data(), Core::ScreenWidth, Core::ScreenHeight);
            SDL_UpdateTexture(upscaleTexture, nullptr, sharpPixels.data(), Core::ScreenWidth * 3);
            renderTexture = upscaleTexture;
        } else {
            SDL_UpdateTexture(texture, nullptr, core.framebuffer().data(), Core::ScreenWidth * static_cast<int>(sizeof(u16)));
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, renderTexture, nullptr, &dst);
        if (showTopMenuBar) {
            drawTopMenuBar(renderer, outputW, openTopMenuSection, hoveredTopMenuSection, hoveredTopMenuItem);
        }
        if (showPanel) {
            const auto snapshot = core.debugSnapshot();
            const auto disasmRows = snapshot.available
                ? buildGbaDisasmRows(core, snapshot.cpu.regs[15], snapshot.cpu.thumb, 5)
                : std::vector<std::string>{};
            const auto memoryRows = buildGbaMemoryRows(core, debugAddress, 8);
            drawGbaDebugPanel(
                renderer,
                outputW,
                outputH,
                std::filesystem::path(core.loadedRomPath()).filename().string(),
                core.coreName(),
                uiMessage,
                snapshot,
                disasmRows,
                memoryRows,
                debugAddress,
                debugRegionIndex,
                debugEdit,
                debugBreakpoints,
                paused,
                muted,
                fastForward,
                fps,
                showBreakpointMenu,
                showSearchPanel
            );
        } else if (!uiMessage.empty()) {
            const int msgY = showTopMenuBar ? topMenuBarHeight() + 8 : 8;
            drawHexText(renderer, dst.x + 12, msgY, uiMessage.substr(0, 42), SDL_Color{255, 230, 120, 255}, 1);
        }
        if (showScaleMenu && fullscreen) {
            drawFullscreenScaleMenu(renderer, outputW, outputH, scaleMenuIndex);
        }
        if (showControlsMenu) {
            drawGbaControlsOverlay(renderer, outputW, outputH);
        }
        SDL_RenderPresent(renderer);

        ++fpsFrames;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double>(now - fpsWindowStart).count();
        if (elapsed >= 0.5) {
            fps = static_cast<double>(fpsFrames) / elapsed;
            fpsFrames = 0;
            fpsWindowStart = now;
        }
        if (uiMessageFrames > 0) {
            --uiMessageFrames;
            if (uiMessageFrames == 0) {
                uiMessage.clear();
            }
        }

        nextFrame += kGbaFrameBudget;
        if (!fastForward) {
            const auto sleepNow = std::chrono::steady_clock::now();
            if (nextFrame < sleepNow - kGbaFrameBudget * 2) {
                nextFrame = sleepNow + kGbaFrameBudget;
            } else if (nextFrame > sleepNow) {
                std::this_thread::sleep_until(nextFrame);
            }
        } else {
            nextFrame = std::chrono::steady_clock::now();
        }
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif
    if (gamepad != nullptr) {
        SDL_GameControllerClose(gamepad);
    }
    if (audioReady) {
        SDL_ClearQueuedAudio(audioDev);
        SDL_AudioStreamClear(audioStream);
        SDL_FreeAudioStream(audioStream);
        SDL_CloseAudioDevice(audioDev);
    }
    SDL_DestroyTexture(texture);
    if (upscaleTexture != nullptr) {
        SDL_DestroyTexture(upscaleTexture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return backToMenu ? 2 : 0;
}

} // namespace

int runGbaLibretroRealtime(
    gba::LibretroCore& core,
    int scale,
    const std::string& statePath,
    const std::string& batteryRamPath,
    const std::string& captureDir
) {
    (void)batteryRamPath;
    std::string title = "GBA libretro";
    if (!core.coreName().empty()) {
        title += " - ";
        title += core.coreName();
    }
    return runGbaRealtimeCommon(core, scale, title, statePath, captureDir);
}

int runGbaMgbaRealtime(
    gba::MgbaCore& core,
    int scale,
    const std::string& statePath,
    const std::string& batteryRamPath,
    const std::string& captureDir
) {
    (void)batteryRamPath;
    std::string title = "GBA emulador pika";
    if (!core.coreName().empty()) {
        title += " - ";
        title += core.coreName();
    }
    return runGbaRealtimeCommon(core, scale, title, statePath, captureDir);
}

} // namespace gb::frontend
#endif
