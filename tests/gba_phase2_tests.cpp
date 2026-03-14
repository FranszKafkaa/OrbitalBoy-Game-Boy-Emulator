#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gb/core/environment.hpp"
#include "gb/core/gba/cpu.hpp"
#include "gb/core/gba/memory.hpp"
#include "gb/core/gba/ppu.hpp"
#include "gb/core/gba/system.hpp"

#include "test_framework.hpp"
#include "test_utils.hpp"

namespace {

void writeWordLe(std::vector<gb::u8>& rom, std::size_t offset, gb::u32 value) {
    if (offset + 4 > rom.size()) {
        return;
    }
    rom[offset + 0] = static_cast<gb::u8>(value & 0xFFU);
    rom[offset + 1] = static_cast<gb::u8>((value >> 8U) & 0xFFU);
    rom[offset + 2] = static_cast<gb::u8>((value >> 16U) & 0xFFU);
    rom[offset + 3] = static_cast<gb::u8>((value >> 24U) & 0xFFU);
}

void writeHalfLe(std::vector<gb::u8>& rom, std::size_t offset, gb::u16 value) {
    if (offset + 2 > rom.size()) {
        return;
    }
    rom[offset + 0] = static_cast<gb::u8>(value & 0xFFU);
    rom[offset + 1] = static_cast<gb::u8>((value >> 8U) & 0xFFU);
}

std::vector<gb::u8> makeArmRom(std::initializer_list<gb::u32> instructions) {
    std::vector<gb::u8> rom(0x200, 0x00);
    std::size_t offset = 0;
    for (const gb::u32 inst : instructions) {
        writeWordLe(rom, offset, inst);
        offset += 4;
    }

    // Header minimo para loadRomFromFile de GBA.
    rom[0xB2] = 0x96;
    return rom;
}

std::vector<gb::u8> makeGbaRomWithBackupTag(
    const std::string& tag,
    const std::string& gameCode = {},
    const std::string& title = {}
) {
    std::vector<gb::u8> rom(0x400, 0x00);
    rom[0xB2] = 0x96;
    if (!title.empty()) {
        const std::size_t titleLen = std::min<std::size_t>(title.size(), 12U);
        for (std::size_t i = 0; i < titleLen; ++i) {
            rom[0xA0U + i] = static_cast<gb::u8>(title[i]);
        }
    }
    if (gameCode.size() == 4U) {
        for (std::size_t i = 0; i < 4U; ++i) {
            rom[0xACU + i] = static_cast<gb::u8>(gameCode[i]);
        }
        rom[0xB0U] = static_cast<gb::u8>('0');
        rom[0xB1U] = static_cast<gb::u8>('1');
    }
    const std::size_t maxLen = std::min<std::size_t>(tag.size(), rom.size() - 0x100U);
    for (std::size_t i = 0; i < maxLen; ++i) {
        rom[0x100U + i] = static_cast<gb::u8>(tag[i]);
    }
    return rom;
}

void eepromWriteBit(gb::gba::Memory& memory, gb::u8 bit) {
    memory.write16(0x0D000000U, static_cast<gb::u16>(bit & 0x1U));
}

gb::u8 eepromReadBit(const gb::gba::Memory& memory) {
    return static_cast<gb::u8>(memory.read16(0x0D000000U) & 0x1U);
}

std::string toLowerAscii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

gb::u16 bgr555ToRgb565(gb::u16 pixel) {
    const gb::u16 r5 = static_cast<gb::u16>(pixel & 0x1FU);
    const gb::u16 g5 = static_cast<gb::u16>((pixel >> 5U) & 0x1FU);
    const gb::u16 b5 = static_cast<gb::u16>((pixel >> 10U) & 0x1FU);
    const gb::u16 g6 = static_cast<gb::u16>((g5 << 1U) | (g5 >> 4U));
    return static_cast<gb::u16>((r5 << 11U) | (g6 << 5U) | b5);
}

template <std::size_t N>
std::size_t countDistinctColors(const std::array<gb::u16, N>& framebuffer, std::size_t stopAfter = 64U) {
    std::vector<gb::u16> colors;
    colors.reserve(std::min<std::size_t>(stopAfter, framebuffer.size()));
    for (gb::u16 pixel : framebuffer) {
        if (std::find(colors.begin(), colors.end(), pixel) != colors.end()) {
            continue;
        }
        colors.push_back(pixel);
        if (colors.size() >= stopAfter) {
            break;
        }
    }
    return colors.size();
}

std::filesystem::path localRomPath(const std::filesystem::path& relativePath) {
    return std::filesystem::current_path() / relativePath;
}

std::vector<std::vector<std::pair<std::string, gb::u32>>> parseDiffTrace(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }

    std::vector<std::vector<std::pair<std::string, gb::u32>>> entries;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream iss(line);
        std::string token;
        std::vector<std::pair<std::string, gb::u32>> parsed;
        while (iss >> token) {
            const auto eq = token.find('=');
            if (eq == std::string::npos || eq == 0U || eq + 1U >= token.size()) {
                continue;
            }
            std::string key = toLowerAscii(token.substr(0U, eq));
            std::string value = token.substr(eq + 1U);
            if (value.size() > 1U && value.back() == ',') {
                value.pop_back();
            }
            parsed.push_back({
                key,
                static_cast<gb::u32>(std::stoul(value, nullptr, 0)),
            });
        }
        if (!parsed.empty()) {
            entries.push_back(std::move(parsed));
        }
    }
    return entries;
}

} // namespace

TEST_CASE("cpu", "gba_memory_maps_rom_ewram_iwram") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    rom[0] = 0xAA;
    rom[1] = 0xBB;
    T_REQUIRE(memory.loadRom(rom));

    T_EQ(memory.read8(0x08000000), static_cast<gb::u8>(0xAA));
    T_EQ(memory.read8(0x08000001), static_cast<gb::u8>(0xBB));

    memory.write32(0x02000010, 0x11223344U);
    T_EQ(memory.read32(0x02000010), 0x11223344U);

    memory.write16(0x03000004, 0x55AAU);
    T_EQ(memory.read16(0x03000004), static_cast<gb::u16>(0x55AA));

    memory.write16(0x06000000, 0x7FFFU);
    T_EQ(memory.read16(0x06000000), static_cast<gb::u16>(0x7FFF));

    memory.write16(0x05000000, 0x001FU);
    T_EQ(memory.read16(0x05000000), static_cast<gb::u16>(0x001F));
}

TEST_CASE("cpu", "gba_memory_unaligned_access_and_vram_mirroring") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.write32(0x02000000U, 0x11223344U);
    T_EQ(memory.read32(0x02000000U), 0x11223344U);
    T_EQ(memory.read32(0x02000001U), 0x44112233U);
    T_EQ(memory.read16(0x02000001U), static_cast<gb::u16>(0x3344U));

    memory.write8(0x06010000U, 0xABU);
    T_EQ(memory.read8(0x06018000U), static_cast<gb::u8>(0xABU));
    memory.write8(0x0601FFFFU, 0xCDU);
    T_EQ(memory.read8(0x06017FFFU), static_cast<gb::u8>(0xCDU));
}

TEST_CASE("cpu", "gba_memory_oam_byte_write_is_ignored") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.write16(0x07000000U, 0x1234U);
    memory.write8(0x07000000U, 0xABU);
    memory.write8(0x07000001U, 0xCDU);
    T_EQ(memory.read16(0x07000000U), static_cast<gb::u16>(0x1234U));
}

TEST_CASE("cpu", "gba_arm_data_processing_and_conditions") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE3A00005U, // MOV r0, #5
        0xE3A01002U, // MOV r1, #2
        0xE0802001U, // ADD r2, r0, r1
        0xE2423001U, // SUB r3, r2, #1
        0xE3530006U, // CMP r3, #6
        0x13A04009U, // MOVNE r4, #9
        0x03A04007U, // MOVEQ r4, #7
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    for (int i = 0; i < 7; ++i) {
        (void)cpu.step();
    }

    T_EQ(cpu.reg(2), 7U);
    T_EQ(cpu.reg(3), 6U);
    T_EQ(cpu.reg(4), 7U);
    T_REQUIRE(cpu.flagZ());
}

TEST_CASE("cpu", "gba_arm_data_processing_register_shift_operand") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE3A00001U, // MOV r0, #1
        0xE3A01004U, // MOV r1, #4
        0xE1A02110U, // MOV r2, r0, LSL r1
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    (void)cpu.step();
    (void)cpu.step();
    (void)cpu.step();

    T_EQ(cpu.reg(2), 16U);
}

TEST_CASE("cpu", "gba_arm_str_ldr_word_transfer") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE5801004U, // STR r1, [r0, #4]
        0xE5902004U, // LDR r2, [r0, #4]
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, 0x02000000U);
    cpu.setReg(1, 0x11223344U);

    (void)cpu.step();
    (void)cpu.step();

    T_EQ(memory.read32(0x02000004U), 0x11223344U);
    T_EQ(cpu.reg(2), 0x11223344U);
}

TEST_CASE("cpu", "gba_arm_str_pc_stores_pipeline_plus_twelve") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE580F000U, // STR pc, [r0]
        0xE5901000U, // LDR r1, [r0]
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, 0x02000000U);

    (void)cpu.step();
    (void)cpu.step();

    T_EQ(memory.read32(0x02000000U), 0x0800000CU);
    T_EQ(cpu.reg(1), 0x0800000CU);
}

TEST_CASE("cpu", "gba_arm_umull_writes_64bit_result") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE3A00003U, // MOV r0, #3
        0xE3A01004U, // MOV r1, #4
        0xE0832190U, // UMULL r2, r3, r0, r1
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    (void)cpu.step();
    (void)cpu.step();
    (void)cpu.step();

    T_EQ(cpu.reg(2), 12U);
    T_EQ(cpu.reg(3), 0U);
}

TEST_CASE("cpu", "gba_arm_swp_exchanges_memory_and_register") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE3A01012U, // MOV r1, #0x12
        0xE5801000U, // STR r1, [r0]
        0xE3A03034U, // MOV r3, #0x34
        0xE1002093U, // SWP r2, r3, [r0]
        0xE5904000U, // LDR r4, [r0]
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, 0x02000000U);

    (void)cpu.step();
    (void)cpu.step();
    (void)cpu.step();
    (void)cpu.step();
    (void)cpu.step();

    T_EQ(cpu.reg(2), 0x12U);
    T_EQ(cpu.reg(4), 0x34U);
}

TEST_CASE("cpu", "gba_arm_branch_skips_instruction") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE3A00001U, // MOV r0, #1
        0xEA000000U, // B +0 (pula o proximo)
        0xE3A00002U, // MOV r0, #2 (pulado)
        0xE3A01003U, // MOV r1, #3
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    (void)cpu.step();
    (void)cpu.step();
    (void)cpu.step();

    T_EQ(cpu.reg(0), 1U);
    T_EQ(cpu.reg(1), 3U);
}

TEST_CASE("cpu", "gba_thumb_execution_and_swi_div") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x400, 0x00);
    writeWordLe(rom, 0x000, 0xE3A00009U); // MOV r0, #9
    writeWordLe(rom, 0x004, 0xE12FFF10U); // BX r0 -> thumb em 0x08000008
    writeHalfLe(rom, 0x008, 0x200AU); // MOV r0, #10
    writeHalfLe(rom, 0x00A, 0x2103U); // MOV r1, #3
    writeHalfLe(rom, 0x00C, 0xDF06U); // SWI 0x06 (Div)
    writeHalfLe(rom, 0x00E, 0x3201U); // ADD r2, #1
    rom[0xB2] = 0x96;
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    (void)cpu.step(); // ARM MOV
    T_EQ(cpu.reg(0), 9U);
    cpu.setReg(0, 0x08000009U);
    (void)cpu.step(); // ARM BX
    T_REQUIRE(cpu.thumbMode());
    T_EQ(cpu.pc(), 0x08000008U);
    (void)cpu.step(); // THUMB MOV r0,#10
    T_EQ(cpu.reg(0), 10U);
    (void)cpu.step(); // THUMB MOV r1,#3
    T_EQ(cpu.reg(1), 3U);
    (void)cpu.step(); // THUMB SWI 0x06
    T_EQ(cpu.reg(0), 3U);
    T_EQ(cpu.reg(1), 1U);
    (void)cpu.step(); // THUMB ADD r2,#1

    T_REQUIRE(cpu.thumbMode());
    T_EQ(cpu.reg(2), 1U);
}

TEST_CASE("cpu", "gba_swi_rluncomp_wram_uses_compressed_length_plus_three") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xEF000014U, // SWI RLUnCompWram
    });
    T_REQUIRE(memory.loadRom(rom));

    const gb::u32 src = 0x02000000U;
    const gb::u32 dst = 0x02000100U;
    // Header RL (0x30) + output size 6 bytes.
    memory.write8(src + 0U, 0x30U);
    memory.write8(src + 1U, 0x06U);
    memory.write8(src + 2U, 0x00U);
    memory.write8(src + 3U, 0x00U);
    // Bloco comprimido: len=(0x02)+3=5, valor=0x7A.
    memory.write8(src + 4U, 0x82U);
    memory.write8(src + 5U, 0x7AU);
    // Bloco literal: len=(0x00)+1=1, byte=0x55.
    memory.write8(src + 6U, 0x00U);
    memory.write8(src + 7U, 0x55U);

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, src);
    cpu.setReg(1, dst);

    (void)cpu.step();

    T_EQ(memory.read8(dst + 0U), static_cast<gb::u8>(0x7AU));
    T_EQ(memory.read8(dst + 1U), static_cast<gb::u8>(0x7AU));
    T_EQ(memory.read8(dst + 2U), static_cast<gb::u8>(0x7AU));
    T_EQ(memory.read8(dst + 3U), static_cast<gb::u8>(0x7AU));
    T_EQ(memory.read8(dst + 4U), static_cast<gb::u8>(0x7AU));
    T_EQ(memory.read8(dst + 5U), static_cast<gb::u8>(0x55U));
}

TEST_CASE("cpu", "gba_swi_rluncomp_vram_packs_bytes_in_halfwords") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xEF000015U, // SWI RLUnCompVram
    });
    T_REQUIRE(memory.loadRom(rom));

    const gb::u32 src = 0x02000040U;
    const gb::u32 dst = 0x06000000U;
    memory.write8(src + 0U, 0x30U);
    memory.write8(src + 1U, 0x06U);
    memory.write8(src + 2U, 0x00U);
    memory.write8(src + 3U, 0x00U);
    memory.write8(src + 4U, 0x82U); // 5x
    memory.write8(src + 5U, 0x7AU);
    memory.write8(src + 6U, 0x00U); // 1 literal
    memory.write8(src + 7U, 0x55U);

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, src);
    cpu.setReg(1, dst);

    (void)cpu.step();

    T_EQ(memory.read16(dst + 0U), static_cast<gb::u16>(0x7A7AU));
    T_EQ(memory.read16(dst + 2U), static_cast<gb::u16>(0x7A7AU));
    T_EQ(memory.read16(dst + 4U), static_cast<gb::u16>(0x557AU));
}

TEST_CASE("cpu", "gba_swi_bgaffineset_uses_8bit_angle_turns") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xEF00000EU, // SWI BgAffineSet
    });
    T_REQUIRE(memory.loadRom(rom));

    const gb::u32 src = 0x02000000U;
    const gb::u32 dst = 0x02000100U;
    memory.write32(src + 0U, 0U); // texX
    memory.write32(src + 4U, 0U); // texY
    memory.write16(src + 8U, 0U); // screenX
    memory.write16(src + 10U, 0U); // screenY
    memory.write16(src + 12U, 0x0100U); // scaleX = 1.0
    memory.write16(src + 14U, 0x0100U); // scaleY = 1.0
    memory.write16(src + 16U, 0x0040U); // 64/256 turn = 90 graus

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, src);
    cpu.setReg(1, dst);
    cpu.setReg(2, 1U);

    (void)cpu.step();

    T_EQ(memory.read16(dst + 0U), static_cast<gb::u16>(0x0000U));
    T_EQ(memory.read16(dst + 2U), static_cast<gb::u16>(0xFF00U));
    T_EQ(memory.read16(dst + 4U), static_cast<gb::u16>(0x0100U));
    T_EQ(memory.read16(dst + 6U), static_cast<gb::u16>(0x0000U));
}

TEST_CASE("cpu", "gba_swi_objaffineset_uses_8bit_angle_turns") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xEF00000FU, // SWI ObjAffineSet
    });
    T_REQUIRE(memory.loadRom(rom));

    const gb::u32 src = 0x02000040U;
    const gb::u32 dst = 0x07000006U;
    memory.write16(src + 0U, 0x0100U); // scaleX = 1.0
    memory.write16(src + 2U, 0x0100U); // scaleY = 1.0
    memory.write16(src + 4U, 0x0040U); // 90 graus

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, src);
    cpu.setReg(1, dst);
    cpu.setReg(2, 1U);
    cpu.setReg(3, 8U);

    (void)cpu.step();

    T_EQ(memory.read16(dst + 0U), static_cast<gb::u16>(0x0000U));
    T_EQ(memory.read16(dst + 8U), static_cast<gb::u16>(0xFF00U));
    T_EQ(memory.read16(dst + 16U), static_cast<gb::u16>(0x0100U));
    T_EQ(memory.read16(dst + 24U), static_cast<gb::u16>(0x0000U));
}

TEST_CASE("cpu", "gba_thumb_bl_reaches_expected_target_and_sets_lr") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    writeWordLe(rom, 0x000, 0xE1A00000U); // ARM: NOP
    writeWordLe(rom, 0x004, 0xE12FFF10U); // ARM: BX r0 -> THUMB em 0x08000008
    writeHalfLe(rom, 0x008, 0xF000U); // THUMB: BL prefix (offset alto = 0)
    writeHalfLe(rom, 0x00A, 0xF804U); // THUMB: BL suffix (offset baixo = 8 bytes)
    writeHalfLe(rom, 0x00C, 0x2001U); // THUMB: MOV r0, #1 (deve ser pulado)
    writeHalfLe(rom, 0x00E, 0x46C0U); // THUMB: NOP
    writeHalfLe(rom, 0x014, 0x2177U); // THUMB: MOV r1, #0x77
    rom[0xB2] = 0x96;
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, 0x08000009U);

    (void)cpu.step(); // ARM NOP
    (void)cpu.step(); // ARM BX r0
    T_REQUIRE(cpu.thumbMode());
    T_EQ(cpu.pc(), 0x08000008U);

    (void)cpu.step(); // BL prefix
    (void)cpu.step(); // BL suffix
    T_EQ(cpu.pc(), 0x08000014U);
    T_EQ(cpu.reg(14), 0x0800000DU);

    (void)cpu.step(); // MOV r1,#0x77
    T_EQ(cpu.reg(1), 0x77U);
    T_EQ(cpu.reg(0), 0x08000009U);
}

TEST_CASE("cpu", "gba_arm_add_pc_operand_uses_pipeline_value_for_bx_thumb_bridge") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    writeWordLe(rom, 0x000, 0xE28F0001U); // ADD r0, pc, #1 -> deve gerar 0x08000009
    writeWordLe(rom, 0x004, 0xE12FFF10U); // BX r0 -> entra em THUMB no endereco 0x08000008
    writeHalfLe(rom, 0x008, 0x212AU); // THUMB: MOV r1, #0x2A
    writeHalfLe(rom, 0x00A, 0xE7FEU); // THUMB: B .
    rom[0xB2] = 0x96;
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    (void)cpu.step(); // ADD r0, pc, #1
    T_EQ(cpu.reg(0), 0x08000009U);
    (void)cpu.step(); // BX r0
    T_REQUIRE(cpu.thumbMode());
    T_EQ(cpu.pc(), 0x08000008U);
    (void)cpu.step(); // MOV r1, #0x2A
    T_EQ(cpu.reg(1), 0x2AU);
}

TEST_CASE("cpu", "gba_thumb_bx_pc_switches_to_aligned_arm_target") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    writeWordLe(rom, 0x000, 0xE1A00000U); // ARM: NOP (r0 ajustado no teste)
    writeWordLe(rom, 0x004, 0xE12FFF10U); // ARM: BX r0 -> THUMB em 0x08000008
    writeHalfLe(rom, 0x008, 0x4778U); // THUMB: BX PC (usa current+4, vai para ARM 0x0800000C)
    writeHalfLe(rom, 0x00A, 0x46C0U); // THUMB: NOP (nao deve executar)
    writeWordLe(rom, 0x00C, 0xE3A01042U); // ARM: MOV r1, #0x42
    rom[0xB2] = 0x96;
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, 0x08000009U);

    (void)cpu.step(); // NOP
    (void)cpu.step(); // BX r0
    T_REQUIRE(cpu.thumbMode());
    T_EQ(cpu.pc(), 0x08000008U);
    (void)cpu.step(); // BX PC
    T_REQUIRE(!cpu.thumbMode());
    T_EQ(cpu.pc(), 0x0800000CU);
    (void)cpu.step(); // MOV r1, #0x42
    T_EQ(cpu.reg(1), 0x42U);
}

TEST_CASE("cpu", "gba_arm_bx_aligns_word_target_in_arm_state") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    writeWordLe(rom, 0x000, 0xE12FFF10U); // ARM: BX r0
    writeWordLe(rom, 0x004, 0xE3A01011U); // ARM: MOV r1, #0x11 (nao deve executar)
    writeWordLe(rom, 0x008, 0xE1A00000U); // ARM: NOP
    writeWordLe(rom, 0x00C, 0xE1A00000U); // ARM: NOP
    writeWordLe(rom, 0x010, 0xE3A02022U); // ARM: MOV r2, #0x22
    rom[0xB2] = 0x96;
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, 0x08000012U); // alvo ARM desalinhado em 2 bytes (deve alinhar para 0x08000010)

    (void)cpu.step(); // BX r0
    T_REQUIRE(!cpu.thumbMode());
    T_EQ(cpu.pc(), 0x08000010U);
    (void)cpu.step(); // MOV r2, #0x22
    T_EQ(cpu.reg(2), 0x22U);
    T_EQ(cpu.reg(1), 0x00U);
}

TEST_CASE("cpu", "gba_arm_bx_pc_uses_pipeline_plus_eight") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    writeWordLe(rom, 0x000, 0xE12FFF1FU); // BX pc
    writeWordLe(rom, 0x004, 0xE3A00011U); // MOV r0, #0x11 (nao deve executar)
    writeWordLe(rom, 0x008, 0xE3A01055U); // MOV r1, #0x55
    rom[0xB2] = 0x96;
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    (void)cpu.step(); // BX pc -> current + 8
    T_REQUIRE(!cpu.thumbMode());
    T_EQ(cpu.pc(), 0x08000008U);
    (void)cpu.step(); // MOV r1, #0x55
    T_EQ(cpu.reg(1), 0x55U);
    T_EQ(cpu.reg(0), 0x00U);
}

TEST_CASE("cpu", "gba_arm_mul_and_block_transfer") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE0020190U, // MUL r2, r0, r1
        0xE8A30007U, // STMIA r3!, {r0-r2}
        0xE8B70070U, // LDMIA r7!, {r4-r6}
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, 2U);
    cpu.setReg(1, 3U);
    cpu.setReg(3, 0x02000000U);
    cpu.setReg(7, 0x02000000U);

    (void)cpu.step();
    (void)cpu.step();
    (void)cpu.step();

    T_EQ(cpu.reg(2), 6U);
    T_EQ(memory.read32(0x02000000U), 2U);
    T_EQ(memory.read32(0x02000004U), 3U);
    T_EQ(memory.read32(0x02000008U), 6U);
    T_EQ(cpu.reg(4), 2U);
    T_EQ(cpu.reg(5), 3U);
    T_EQ(cpu.reg(6), 6U);
    T_EQ(cpu.reg(3), 0x0200000CU);
    T_EQ(cpu.reg(7), 0x0200000CU);
}

TEST_CASE("cpu", "gba_arm_msr_cpsr_switches_banked_stack_pointer") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE3A0D011U, // MOV r13, #0x11 (SYS/USR SP)
        0xE3A01012U, // MOV r1, #0x12 (IRQ mode)
        0xE121F001U, // MSR CPSR_c, r1
        0xE3A0D022U, // MOV r13, #0x22 (IRQ SP)
        0xE3A0101FU, // MOV r1, #0x1F (SYS mode)
        0xE121F001U, // MSR CPSR_c, r1
        0xE1A0400DU, // MOV r4, r13 (captura SP SYS)
        0xE3A01012U, // MOV r1, #0x12 (IRQ mode)
        0xE121F001U, // MSR CPSR_c, r1
        0xE1A0500DU, // MOV r5, r13 (captura SP IRQ)
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    for (int i = 0; i < 10; ++i) {
        (void)cpu.step();
    }

    T_EQ(cpu.reg(4), 0x11U);
    T_EQ(cpu.reg(5), 0x22U);
    T_EQ(cpu.cpsr() & 0x1FU, 0x12U);
}

TEST_CASE("cpu", "gba_arm_fiq_mode_banks_r8_to_r12") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE3A08011U, // MOV r8, #0x11 (shared bank)
        0xE3A00011U, // MOV r0, #0x11 (FIQ)
        0xE121F000U, // MSR CPSR_c, r0
        0xE3A08022U, // MOV r8, #0x22 (FIQ bank)
        0xE3A0001FU, // MOV r0, #0x1F (SYS)
        0xE121F000U, // MSR CPSR_c, r0
        0xE1A01008U, // MOV r1, r8 (shared bank)
        0xE3A00011U, // MOV r0, #0x11 (FIQ)
        0xE121F000U, // MSR CPSR_c, r0
        0xE1A02008U, // MOV r2, r8 (FIQ bank)
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    for (int i = 0; i < 10; ++i) {
        (void)cpu.step();
    }

    T_EQ(cpu.reg(1), 0x11U);
    T_EQ(cpu.reg(2), 0x22U);
    T_EQ(cpu.cpsr() & 0x1FU, 0x11U);
}

TEST_CASE("cpu", "gba_arm_ldm_stm_with_s_bit_uses_user_bank_registers") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE3A08011U, // MOV r8, #0x11 (user/shared)
        0xE3A09033U, // MOV r9, #0x33 (user/shared)
        0xE3A00011U, // MOV r0, #0x11 (FIQ mode)
        0xE121F000U, // MSR CPSR_c, r0
        0xE3A08022U, // MOV r8, #0x22 (FIQ bank)
        0xE3A09044U, // MOV r9, #0x44 (FIQ bank)
        0xE8E20100U, // STMIA r2!, {r8}^  -> escreve user r8
        0xE8F20200U, // LDMIA r2!, {r9}^  -> carrega user r9
        0xE1A04009U, // MOV r4, r9 (captura r9 FIQ)
        0xE3A0001FU, // MOV r0, #0x1F (SYS mode)
        0xE121F000U, // MSR CPSR_c, r0
        0xE1A05009U, // MOV r5, r9 (captura user r9)
    });
    T_REQUIRE(memory.loadRom(rom));
    memory.write32(0x02000004U, 0x00000077U);

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(2, 0x02000000U);

    for (int i = 0; i < 12; ++i) {
        (void)cpu.step();
    }

    T_EQ(memory.read32(0x02000000U), 0x00000011U);
    T_EQ(cpu.reg(4), 0x44U);
    T_EQ(cpu.reg(5), 0x77U);
    T_EQ(cpu.reg(2), 0x02000008U);
    T_EQ(cpu.cpsr() & 0x1FU, 0x1FU);
}

TEST_CASE("cpu", "gba_arm_unknown_opcode_enters_undefined_exception") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xEE000010U, // CDP coprocessor (inexistente no ARM7TDMI/GBA)
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    (void)cpu.step();

    T_EQ(cpu.cpsr() & 0x1FU, 0x1BU); // UND
    T_EQ(cpu.cpsr() & (1U << 7U), 1U << 7U); // I mask
    T_EQ(cpu.pc(), 0x00000004U);
    T_EQ(cpu.reg(14), 0x08000004U); // LR_und
}

TEST_CASE("cpu", "gba_arm_invalid_execute_address_triggers_prefetch_abort_exception") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE12FFF10U, // BX r0
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();
    cpu.setReg(0, 0x01000000U); // regiao invalida para fetch

    (void)cpu.step(); // BX r0
    T_EQ(cpu.pc(), 0x01000000U);
    (void)cpu.step(); // prefetch abort

    T_EQ(cpu.cpsr() & 0x1FU, 0x17U); // ABT
    T_EQ(cpu.cpsr() & (1U << 7U), 1U << 7U); // I mask
    T_EQ(cpu.pc(), 0x0000000CU);
    T_EQ(cpu.reg(14), 0x01000004U); // LR_abt
}

TEST_CASE("cpu", "gba_cpu_optional_diff_trace_matches_reference_file") {
    const auto tracePathEnv = gb::readEnvironmentVariable("GBEMU_GBA_MGBA_TRACE");
    if (!tracePathEnv.has_value() || tracePathEnv->empty()) {
        return; // opcional: executa apenas quando um trace externo for fornecido.
    }

    const std::filesystem::path tracePath(*tracePathEnv);
    if (!std::filesystem::exists(tracePath)) {
        return;
    }

    const auto trace = parseDiffTrace(tracePath);
    T_REQUIRE(!trace.empty());

    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE3A00001U, // MOV r0, #1
        0xE2800002U, // ADD r0, r0, #2
        0xE2401001U, // SUB r1, r0, #1
        0xE0202001U, // EOR r2, r0, r1
        0xEAFFFFFEU, // B .
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    for (const auto& step : trace) {
        (void)cpu.step();
        for (const auto& [key, expected] : step) {
            if (key == "pc") {
                T_EQ(cpu.pc(), expected);
                continue;
            }
            if (key == "cpsr") {
                T_EQ(cpu.cpsr(), expected);
                continue;
            }
            if (key.size() >= 2U && key[0] == 'r') {
                const int reg = std::stoi(key.substr(1U));
                if (reg >= 0 && reg <= 15) {
                    T_EQ(cpu.reg(reg), expected);
                }
            }
        }
    }
}

TEST_CASE("cpu", "gba_arm_subs_pc_restores_cpsr_from_spsr") {
    gb::gba::Memory memory;
    const auto rom = makeArmRom({
        0xE3A00012U, // MOV r0, #0x12 (IRQ mode)
        0xE121F000U, // MSR CPSR_c, r0
        0xE3A0103FU, // MOV r1, #0x3F (SYS + Thumb bit)
        0xE16FF001U, // MSR SPSR_fsxc, r1
        0xE3A0E021U, // MOV lr, #0x21
        0xE25EF000U, // SUBS pc, lr, #0 -> 0x08000020 em THUMB
    });
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    (void)cpu.step();
    (void)cpu.step();
    (void)cpu.step();
    (void)cpu.step();
    (void)cpu.step(); // LR = 0x21
    cpu.setReg(14, cpu.reg(14) | 0x08000000U);
    (void)cpu.step(); // SUBS pc, lr, #0

    T_REQUIRE(cpu.thumbMode());
    T_EQ(cpu.cpsr() & 0x1FU, 0x1FU);
    T_EQ(cpu.pc(), 0x08000020U);
}

TEST_CASE("cpu", "gba_cpu_halt_and_irq_dispatch") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x400, 0x00);
    writeWordLe(rom, 0x000, 0xEF000002U); // SWI Halt
    writeWordLe(rom, 0x004, 0xE3A00001U); // MOV r0, #1
    writeWordLe(rom, 0x040, 0xE3A0002AU); // IRQ handler: MOV r0, #42
    writeWordLe(rom, 0x044, 0xE12FFF1EU); // BX LR
    rom[0xB2] = 0x96;
    T_REQUIRE(memory.loadRom(rom));
    memory.write32(0x03007FFCU, 0x08000040U);
    memory.writeIo16(gb::gba::Memory::IeOffset, 0x0001U);
    memory.writeIo16(gb::gba::Memory::ImeOffset, 0x0001U);

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    (void)cpu.step(); // entra em halt
    const auto haltedPc = cpu.pc();
    (void)cpu.step(); // segue parado
    T_EQ(cpu.pc(), haltedPc);

    memory.requestInterrupt(0x0001U);
    (void)cpu.step(); // entra no handler
    T_EQ(cpu.reg(0), 42U);
    memory.clearInterrupt(0x0001U); // handler real precisa reconhecer/limpar IF
    (void)cpu.step(); // BX LR
    T_EQ(cpu.pc(), haltedPc);
    T_EQ(cpu.cpsr() & 0x1FU, 0x1FU);
    (void)cpu.step(); // MOV r0, #1
    T_EQ(cpu.reg(0), 1U);
}

TEST_CASE("cpu", "gba_cpu_irq_thumb_pop_bx_trampoline_restores_sp") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x400, 0x00);
    writeWordLe(rom, 0x000, 0xEF000002U); // SWI Halt
    writeWordLe(rom, 0x004, 0xE1A00000U); // NOP
    writeHalfLe(rom, 0x040, 0xBC01U); // THUMB: POP {r0}
    writeHalfLe(rom, 0x042, 0x4700U); // THUMB: BX r0
    rom[0xB2] = 0x96;
    T_REQUIRE(memory.loadRom(rom));

    memory.write32(0x03007FFCU, 0x08000041U); // handler THUMB
    memory.writeIo16(gb::gba::Memory::IeOffset, 0x0001U);
    memory.writeIo16(gb::gba::Memory::ImeOffset, 0x0001U);

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    const gb::u32 initialSp = cpu.reg(13);
    (void)cpu.step(); // entra em halt
    const gb::u32 resumePc = cpu.pc();
    memory.requestInterrupt(0x0001U);

    (void)cpu.step(); // IRQ + POP {r0}
    (void)cpu.step(); // BX r0 -> trampoline -> restaura contexto

    T_EQ(cpu.pc(), resumePc);
    T_EQ(cpu.reg(13), initialSp);
    T_EQ(cpu.cpsr() & 0x1FU, 0x1FU);
}

TEST_CASE("cpu", "gba_swi_vblank_intr_wait_wakes_only_on_masked_flag") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    writeWordLe(rom, 0x000, 0xEF000005U); // SWI VBlankIntrWait
    writeWordLe(rom, 0x004, 0xE3A00033U); // MOV r0, #0x33 (executa apos wake)
    rom[0xB2] = 0x96;
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::CpuArm7tdmi cpu;
    cpu.connectMemory(&memory);
    cpu.reset();

    (void)cpu.step(); // entra em espera por VBlank
    const gb::u32 waitPc = cpu.pc();
    T_EQ(waitPc, 0x08000004U);

    memory.requestInterrupt(static_cast<gb::u16>(1U << 1U)); // HBlank (nao deve acordar)
    (void)cpu.step();
    T_EQ(cpu.pc(), waitPc);
    T_EQ(cpu.reg(0), 0U);

    memory.requestInterrupt(static_cast<gb::u16>(1U << 0U)); // VBlank
    (void)cpu.step(); // acorda + executa MOV
    T_EQ(cpu.reg(0), 0x33U);
    T_EQ(memory.interruptFlagsRaw() & static_cast<gb::u16>(1U << 0U), static_cast<gb::u16>(0U));
}

TEST_CASE("state", "gba_system_runframe_executes_cpu_phase2") {
    const auto romPath = tests::makeTempPath("gba_phase2", ".gba");
    tests::ScopedPath cleanupRom(romPath);

    const auto image = makeArmRom({
        0xE3A00000U, // MOV r0, #0
        0xE2800001U, // ADD r0, r0, #1
        0xEAFFFFFDU, // B para ADD (loop)
    });
    T_REQUIRE(tests::writeBinaryFile(romPath, image));

    gb::gba::System system;
    T_REQUIRE(system.loadRomFromFile(romPath.string()));

    const auto beforeInstructions = system.cpu().executedInstructions();
    system.runFrame();

    T_REQUIRE(system.cpu().executedInstructions() > beforeInstructions);
    T_REQUIRE(system.cpu().reg(0) > 0U);
}

TEST_CASE("cpu", "gba_ppu_mode3_renders_vram_pixels") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));
    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x0003U); // mode 3
    memory.write16(0x06000000U, 0x001FU); // vermelho
    memory.write16(0x06000002U, 0x03E0U); // verde

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0xF800));
    T_EQ(fb[1], static_cast<gb::u16>(0x07E0));
}

TEST_CASE("cpu", "gba_ppu_mode4_uses_palette_ram") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));
    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x0004U); // mode 4
    memory.write16(0x05000002U, 0x7C00U); // palette[1] = azul
    memory.write8(0x06000000U, 0x01U); // pixel usa indice 1

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x001F));
}

TEST_CASE("cpu", "gba_ppu_mode0_bg0_renders_text_tile") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x0100U); // mode 0 + BG0 enable
    memory.writeIo16(0x0008U, 0x0100U); // BG0CNT: screen base block 1, 4bpp
    memory.write16(0x05000006U, 0x03E0U); // palette index 3 = verde
    memory.write8(0x06000000U, 0x03U); // tile0 pixel(0,0)=3
    memory.write16(0x06000800U, 0x0000U); // map (0,0) aponta tile 0

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x07E0));
}

TEST_CASE("cpu", "gba_memory_timer_irq_and_dma_immediate") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Memory::IeOffset, static_cast<gb::u16>(1U << 3U));
    memory.writeIo16(gb::gba::Memory::ImeOffset, 1U);
    memory.writeIo16(0x0100U, 0xFFFEU); // TM0 reload/counter
    memory.writeIo16(0x0102U, 0x00C0U); // timer enable + irq + prescaler 1
    memory.step(2);
    T_REQUIRE((memory.interruptFlagsRaw() & static_cast<gb::u16>(1U << 3U)) != 0U);
    T_EQ(memory.readIo16(0x0100U), static_cast<gb::u16>(0xFFFEU));

    memory.write16(0x02000000U, 0x1122U);
    memory.write16(0x02000002U, 0x3344U);
    memory.write32(0x040000B0U, 0x02000000U); // DMA0 source
    memory.write32(0x040000B4U, 0x02000100U); // DMA0 dest
    memory.write16(0x040000B8U, 2U); // count
    memory.write16(0x040000BAU, 0xC000U); // enable + immediate + 16-bit + IRQ
    T_EQ(memory.read16(0x02000100U), static_cast<gb::u16>(0x1122U));
    T_EQ(memory.read16(0x02000102U), static_cast<gb::u16>(0x3344U));
    T_REQUIRE((memory.interruptFlagsRaw() & static_cast<gb::u16>(1U << 8U)) != 0U);
}

TEST_CASE("cpu", "gba_memory_keypad_irq") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Memory::KeyControlOffset, static_cast<gb::u16>((1U << 14U) | (1U << 0U)));
    memory.setKeyInputRaw(static_cast<gb::u16>(gb::gba::Memory::DefaultKeyInput & ~static_cast<gb::u16>(1U << 0U)));
    T_REQUIRE((memory.interruptFlagsRaw() & static_cast<gb::u16>(1U << 12U)) != 0U);
}

TEST_CASE("cpu", "gba_memory_keyinput_keeps_upper_bits_set") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.setKeyInputRaw(0x0000U); // pressiona tudo
    T_EQ(memory.keyInputRaw(), static_cast<gb::u16>(0xFC00U));
    memory.setKeyInputRaw(0x03FFU); // solta tudo
    T_EQ(memory.keyInputRaw(), static_cast<gb::u16>(0xFFFFU));
}

TEST_CASE("cpu", "gba_memory_dma_vblank_trigger") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.write16(0x02000000U, 0xA1B2U);
    memory.write16(0x02000002U, 0xC3D4U);
    memory.write32(0x040000B0U, 0x02000000U); // DMA0 source
    memory.write32(0x040000B4U, 0x02000120U); // DMA0 dest
    memory.write16(0x040000B8U, 2U); // count
    memory.write16(0x040000BAU, 0x9000U); // enable + start timing VBlank

    T_EQ(memory.read16(0x02000120U), static_cast<gb::u16>(0x0000));
    memory.triggerDmaStart(1U);
    T_EQ(memory.read16(0x02000120U), static_cast<gb::u16>(0xA1B2U));
    T_EQ(memory.read16(0x02000122U), static_cast<gb::u16>(0xC3D4U));
}

TEST_CASE("cpu", "gba_ppu_mode0_layers_respect_priority") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x0300U); // mode0 + BG0 + BG1
    memory.writeIo16(0x0008U, 0x0101U); // BG0 prio 1, screenblock 1
    memory.writeIo16(0x000AU, 0x0200U); // BG1 prio 0, screenblock 2
    memory.write16(0x05000002U, 0x7C00U); // palette 1 red
    memory.write16(0x05000004U, 0x03E0U); // palette 2 green
    memory.write8(0x06000000U, 0x01U); // tile0 pixel(0,0)=1
    memory.write8(0x06000020U, 0x02U); // tile1 pixel(0,0)=2
    memory.write16(0x06000800U, 0x0000U); // BG0 map -> tile 0
    memory.write16(0x06001000U, 0x0001U); // BG1 map -> tile 1

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x07E0)); // BG1 (prio 0) vence BG0 (prio 1)
}

TEST_CASE("cpu", "gba_ppu_mode0_obj_over_bg") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x1100U); // mode0 + BG0 + OBJ
    memory.writeIo16(0x0008U, 0x0102U); // BG0 prio 2, screenblock 1
    memory.write16(0x05000002U, 0x03E0U); // BG palette 1 = green
    memory.write8(0x06000000U, 0x01U);
    memory.write16(0x06000800U, 0x0000U);

    memory.write16(0x05000204U, 0x001FU); // OBJ palette 2 = red
    memory.write8(0x06010000U, 0x02U); // OBJ tile0 pixel(0,0)=2
    memory.write16(0x07000000U, 0x0000U); // attr0: y=0, square, 4bpp
    memory.write16(0x07000002U, 0x0000U); // attr1: x=0, size 0 (8x8)
    memory.write16(0x07000004U, 0x0000U); // attr2: tile 0, prio 0

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0xF800));
}

TEST_CASE("cpu", "gba_ppu_mode0_obj_8bpp_uses_32byte_tile_units") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x1000U); // mode0 + OBJ
    memory.write16(0x05000202U, 0x001FU); // OBJ palette index 1 = red
    memory.write8(0x06010040U, 0x01U); // tile number 2 (32-byte units), pixel(0,0)=1

    memory.write16(0x07000000U, 0x2000U); // attr0: y=0, square, 8bpp
    memory.write16(0x07000002U, 0x0000U); // attr1: x=0, size 0
    memory.write16(0x07000004U, 0x0002U); // attr2: tile number 2

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0xF800));
}

TEST_CASE("cpu", "gba_ppu_mode0_bg_tile_index_wraps_in_bg_vram_space") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x0100U); // mode0 + BG0
    memory.writeIo16(0x0008U, 0x010CU); // BG0CNT: char block 3, screen block 1
    memory.write16(0x05000002U, 0x03E0U); // palette index 1 = green
    memory.write16(0x05000004U, 0x001FU); // palette index 2 = red

    // tile index 512 em 4bpp com charblock 3:
    // addr = 0xC000 + 512*32 = 0x10000 (deve wrap para 0x0000 no espaco BG).
    memory.write16(0x06000800U, 0x0200U); // BG map entry -> tile 512
    memory.write8(0x06000000U, 0x01U); // cor esperada (BG area wrapped): index 1 = green
    memory.write8(0x06010000U, 0x02U); // area OBJ (nao deve ser lida): index 2 = red

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x07E0U)); // verde
}

TEST_CASE("cpu", "gba_ppu_window_masks_bg_layer") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x2100U); // mode0 + BG0 + WIN0
    memory.writeIo16(0x0008U, 0x0100U); // BG0CNT: screen base block 1
    memory.write16(0x05000002U, 0x03E0U); // palette index 1 = green
    memory.write8(0x06000000U, 0x01U); // tile0 pixel(0,0)=1
    memory.write16(0x06000800U, 0x0000U); // BG0 map -> tile0

    memory.writeIo16(0x0040U, 0x00F0U); // WIN0H: x=0..240
    memory.writeIo16(0x0044U, 0x00A0U); // WIN0V: y=0..160
    memory.writeIo16(0x0048U, 0x0000U); // WININ: dentro de WIN0 nenhuma layer habilitada
    memory.writeIo16(0x004AU, 0x003FU); // WINOUT: fora da janela tudo habilitado

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x0000)); // backdrop (preto)
}

TEST_CASE("cpu", "gba_ppu_window_equal_range_covers_full_screen") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x2100U); // mode0 + BG0 + WIN0
    memory.writeIo16(0x0008U, 0x0100U); // BG0CNT: screen base block 1
    memory.write16(0x05000002U, 0x03E0U); // palette index 1 = green
    memory.write8(0x06000000U, 0x01U); // tile0 pixel(0,0)=1
    memory.write16(0x06000800U, 0x0000U); // BG0 map -> tile0

    // WIN0 com inicio=fim cobre toda a tela neste frontend.
    memory.writeIo16(0x0040U, 0x0000U); // WIN0H: x=0..0
    memory.writeIo16(0x0044U, 0x0000U); // WIN0V: y=0..0
    memory.writeIo16(0x0048U, 0x0000U); // WININ: dentro de WIN0 nenhuma layer habilitada
    memory.writeIo16(0x004AU, 0x003FU); // WINOUT: fora da janela tudo habilitado

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x0000U)); // BG0 mascarado em toda a tela
}

TEST_CASE("cpu", "gba_ppu_window_registers_are_scanline_snapshotted") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x2100U); // mode0 + BG0 + WIN0
    memory.writeIo16(0x0008U, 0x0100U); // BG0CNT
    memory.write16(0x05000002U, 0x03E0U); // BG palette index 1 = green
    for (gb::u32 i = 0; i < 32U; ++i) {
        memory.write8(0x06000000U + i, 0x11U); // tile0 inteiro = indice 1
    }
    memory.write16(0x06000800U, 0x0000U); // BG0 map -> tile0

    memory.writeIo16(0x0040U, 0x00F0U); // WIN0H: tela toda
    memory.writeIo16(0x0044U, 0x00A0U); // WIN0V: tela toda
    memory.writeIo16(0x0048U, 0x0000U); // WININ: BG0 desligado dentro da janela
    memory.writeIo16(0x004AU, 0x003FU); // WINOUT: tudo ligado fora

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    ppu.step(static_cast<int>(gb::gba::Ppu::CyclesPerLine));
    memory.writeIo16(0x0048U, 0x0001U); // habilita BG0 na janela
    ppu.step(static_cast<int>(gb::gba::Ppu::CyclesPerLine));

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));

    const std::size_t line0 = 0U;
    const std::size_t line2 = static_cast<std::size_t>(2U) * static_cast<std::size_t>(gb::gba::Ppu::ScreenWidth);
    T_EQ(fb[line0], static_cast<gb::u16>(0x0000U));
    T_EQ(fb[line2], static_cast<gb::u16>(0x07E0U));
}

TEST_CASE("cpu", "gba_ppu_obj_window_uses_winout_upper_mask") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x9100U); // mode0 + BG0 + OBJ + OBJWIN
    memory.writeIo16(0x0008U, 0x0100U); // BG0CNT: screen base block 1
    memory.write16(0x05000002U, 0x03E0U); // BG palette index 1 = green
    memory.write8(0x06000000U, 0x01U); // BG tile0 pixel(0,0)=1
    memory.write16(0x06000800U, 0x0000U); // BG0 map -> tile0

    memory.write16(0x05000202U, 0x001FU); // OBJ palette index 1 = red
    memory.write8(0x06010000U, 0x01U); // OBJ tile0 pixel(0,0)=1

    // OBJ normal (deveria ficar vermelho sem OBJ window).
    memory.write16(0x07000000U, 0x0000U); // attr0: y=0, normal, 4bpp
    memory.write16(0x07000002U, 0x0000U); // attr1: x=0, size0
    memory.write16(0x07000004U, 0x0000U); // attr2: tile0

    // OBJ window cobrindo o mesmo pixel.
    memory.write16(0x07000008U, 0x0800U); // attr0: y=0, OBJ window
    memory.write16(0x0700000AU, 0x0000U); // attr1: x=0, size0
    memory.write16(0x0700000CU, 0x0000U); // attr2: tile0

    // WINOUT: fora de janelas tudo habilitado; dentro de OBJWIN habilita apenas BG0.
    memory.writeIo16(0x004AU, 0x013FU);

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x07E0U)); // verde (BG0), nao vermelho (OBJ)
}

TEST_CASE("cpu", "gba_ppu_semitransparent_obj_alpha_blends_with_bg") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x1100U); // mode0 + BG0 + OBJ
    memory.writeIo16(0x0008U, 0x0101U); // BG0 prio 1, screen base block 1
    memory.write16(0x05000002U, 0x03E0U); // BG color index 1 = green
    memory.write8(0x06000000U, 0x01U);
    memory.write16(0x06000800U, 0x0000U);

    memory.write16(0x05000204U, 0x001FU); // OBJ color index 2 = red
    memory.write8(0x06010020U, 0x02U); // OBJ tile1 pixel(0,0)=2 (tile0 permanece transparente)
    memory.write16(0x07000000U, 0x0400U); // attr0: y=0, semi-transparent, 4bpp
    memory.write16(0x07000002U, 0x0000U); // attr1: x=0, size0
    memory.write16(0x07000004U, 0x0001U); // attr2: tile1, prio0

    memory.writeIo16(0x0050U, 0x0150U); // BLDCNT: OBJ 1st target + alpha + BG0 2nd target
    memory.writeIo16(0x0052U, 0x0808U); // BLDALPHA: eva=8 evb=8

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x7BC0)); // blend 50/50 de red + green
}

TEST_CASE("cpu", "gba_ppu_blend_registers_are_scanline_snapshotted") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x0100U); // mode0 + BG0
    memory.writeIo16(0x0008U, 0x0100U); // BG0CNT
    memory.write16(0x05000002U, 0x0001U); // BG palette index 1 = vermelho fraco
    for (gb::u32 i = 0; i < 32U; ++i) {
        memory.write8(0x06000000U + i, 0x11U); // tile0 inteiro = indice 1
    }
    memory.write16(0x06000800U, 0x0000U);

    memory.writeIo16(0x0050U, 0x0081U); // BLDCNT: BG0 1st target + brighten
    memory.writeIo16(0x0054U, 0x0000U); // BLDY = 0

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    ppu.step(static_cast<int>(gb::gba::Ppu::CyclesPerLine));
    memory.writeIo16(0x0054U, 0x0010U); // BLDY = 16 (max)
    ppu.step(static_cast<int>(gb::gba::Ppu::CyclesPerLine));

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));

    const std::size_t line0 = 0U;
    const std::size_t line2 = static_cast<std::size_t>(2U) * static_cast<std::size_t>(gb::gba::Ppu::ScreenWidth);
    T_EQ(fb[line0], bgr555ToRgb565(0x0001U));
    T_EQ(fb[line2], static_cast<gb::u16>(0xFFFFU));
}

TEST_CASE("cpu", "gba_ppu_hofs_registers_are_scanline_snapshotted") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x0100U); // mode0 + BG0
    memory.writeIo16(0x0008U, 0x0100U); // BG0CNT
    memory.write16(0x05000002U, 0x03E0U); // index 1 = green
    memory.write16(0x05000004U, 0x001FU); // index 2 = red

    for (gb::u32 i = 0; i < 32U; ++i) {
        memory.write8(0x06000000U + i, 0x11U); // tile0 inteiro = indice 1
        memory.write8(0x06000020U + i, 0x22U); // tile1 inteiro = indice 2
    }
    memory.write16(0x06000800U, 0x0000U); // map(0,0)=tile0
    memory.write16(0x06000802U, 0x0001U); // map(1,0)=tile1
    memory.writeIo16(0x0010U, 0x0000U); // BG0HOFS

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    ppu.step(static_cast<int>(gb::gba::Ppu::CyclesPerLine));
    memory.writeIo16(0x0010U, 0x0008U); // desloca 1 tile a partir da linha 2
    ppu.step(static_cast<int>(gb::gba::Ppu::CyclesPerLine));

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));

    const std::size_t line0 = 0U;
    const std::size_t line2 = static_cast<std::size_t>(2U) * static_cast<std::size_t>(gb::gba::Ppu::ScreenWidth);
    T_EQ(fb[line0], static_cast<gb::u16>(0x07E0U));
    T_EQ(fb[line2], static_cast<gb::u16>(0xF800U));
}

TEST_CASE("cpu", "gba_ppu_render_uses_completed_frame_snapshots_after_wrap") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x0100U); // mode0 + BG0
    memory.writeIo16(0x0008U, 0x0100U); // BG0CNT
    memory.write16(0x05000002U, 0x03E0U); // index 1 = green
    memory.write16(0x05000004U, 0x001FU); // index 2 = red

    for (gb::u32 i = 0; i < 32U; ++i) {
        memory.write8(0x06000000U + i, 0x11U); // tile0 inteiro = indice 1
        memory.write8(0x06000020U + i, 0x22U); // tile1 inteiro = indice 2
    }
    memory.write16(0x06000800U, 0x0000U); // map(0,0)=tile0
    memory.write16(0x06000802U, 0x0001U); // map(1,0)=tile1
    memory.writeIo16(0x0010U, 0x0000U); // BG0HOFS

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    ppu.step(static_cast<int>(gb::gba::Ppu::CyclesPerLine * gb::gba::Ppu::TotalLines));
    memory.writeIo16(0x0010U, 0x0008U); // novo frame ja iniciou, mas o render deve usar o frame anterior completo

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));

    const std::size_t line0 = 0U;
    const std::size_t line2 = static_cast<std::size_t>(2U) * static_cast<std::size_t>(gb::gba::Ppu::ScreenWidth);
    T_EQ(fb[line0], static_cast<gb::u16>(0x07E0U));
    T_EQ(fb[line2], static_cast<gb::u16>(0x07E0U));
}

TEST_CASE("cpu", "gba_ppu_mode4_uses_obj_tile_base_0x14000") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x1004U); // mode4 + OBJ
    memory.write16(0x05000202U, 0x7C00U); // OBJ palette index 1 = blue
    memory.write8(0x06014000U, 0x01U); // OBJ tile0 (4bpp) pixel(0,0)=1

    memory.write16(0x07000000U, 0x0000U); // attr0: y=0, square, 4bpp
    memory.write16(0x07000002U, 0x0000U); // attr1: x=0, size 0
    memory.write16(0x07000004U, 0x0000U); // attr2: tile 0

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x001FU));
}

TEST_CASE("cpu", "gba_ppu_mode1_bg2_affine_renders_tile") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x0401U); // mode1 + BG2
    memory.writeIo16(0x000CU, 0x2100U); // BG2CNT: screen block 1, affine wrap
    memory.writeIo16(0x0020U, 0x0100U); // BG2PA
    memory.writeIo16(0x0022U, 0x0000U); // BG2PB
    memory.writeIo16(0x0024U, 0x0000U); // BG2PC
    memory.writeIo16(0x0026U, 0x0100U); // BG2PD
    memory.write32(0x04000028U, 0x00000000U); // BG2X
    memory.write32(0x0400002CU, 0x00000000U); // BG2Y

    memory.write16(0x05000004U, 0x03E0U); // BG palette index 2 = green
    memory.write8(0x06000000U, 0x02U); // tile0 pixel(0,0)=2
    memory.write8(0x06000800U, 0x00U); // map(0,0)=tile0

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x07E0U));
}

TEST_CASE("cpu", "gba_ppu_ignores_disabled_non_affine_obj") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    memory.writeIo16(gb::gba::Ppu::DispcntOffset, 0x1000U); // mode0 + OBJ
    memory.write16(0x05000202U, 0x001FU); // OBJ palette index 1 = red
    memory.write8(0x06010000U, 0x01U); // tile0 pixel(0,0)=1

    for (int i = 0; i < 128; ++i) {
        memory.write16(0x07000000U + static_cast<gb::u32>(i) * 8U, 0x0200U); // disable
    }

    memory.write16(0x07000000U, 0x0200U); // attr0: OBJ disabled (bit9=1, non-affine)
    memory.write16(0x07000002U, 0x0000U); // attr1
    memory.write16(0x07000004U, 0x0000U); // attr2

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    std::array<gb::u16, gb::gba::Ppu::FramebufferSize> fb{};
    T_REQUIRE(ppu.render(fb));
    T_EQ(fb[0], static_cast<gb::u16>(0x0000U)); // sem sprite desenhado
}

TEST_CASE("cpu", "gba_ppu_updates_vcount_and_blank_flags") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();

    ppu.step(static_cast<int>(gb::gba::Ppu::HblankStartCycle));
    T_REQUIRE(ppu.inHblank());
    T_REQUIRE((memory.readIo16(gb::gba::Ppu::DispstatOffset) & 0x0002U) != 0U);

    ppu.step(static_cast<int>(gb::gba::Ppu::CyclesPerLine * gb::gba::Ppu::VisibleLines));
    T_REQUIRE(ppu.inVblank());
    T_EQ(memory.readIo16(gb::gba::Ppu::VcountOffset), gb::gba::Ppu::VisibleLines);
}

TEST_CASE("cpu", "gba_ppu_vblank_irq_flag_when_enabled") {
    gb::gba::Memory memory;
    std::vector<gb::u8> rom(0x200, 0x00);
    T_REQUIRE(memory.loadRom(rom));

    gb::gba::Ppu ppu;
    ppu.connectMemory(&memory);
    ppu.reset();
    memory.writeIo16(gb::gba::Ppu::DispstatOffset, 0x0008U); // VBlank IRQ enable

    ppu.step(static_cast<int>(gb::gba::Ppu::CyclesPerLine * gb::gba::Ppu::VisibleLines));
    T_REQUIRE((memory.interruptFlagsRaw() & static_cast<gb::u16>(1U << 0U)) != 0U);
}

TEST_CASE("cpu", "gba_backup_sram_read_write_roundtrip") {
    gb::gba::Memory memory;
    const auto rom = makeGbaRomWithBackupTag("SRAM_V113");
    T_REQUIRE(memory.loadRom(rom));
    T_REQUIRE(memory.hasPersistentBackup());
    T_EQ(memory.backupTypeName(), std::string("SRAM"));

    memory.write8(0x0E000010U, 0x42U);
    memory.write8(0x0E008010U, 0x24U); // espelho

    T_EQ(memory.read8(0x0E000010U), static_cast<gb::u8>(0x24U));
    T_EQ(memory.read8(0x0E008010U), static_cast<gb::u8>(0x24U));
}

TEST_CASE("cpu", "gba_backup_flash_accepts_program_and_id_mode") {
    gb::gba::Memory memory;
    const auto rom = makeGbaRomWithBackupTag("FLASH1M_V102");
    T_REQUIRE(memory.loadRom(rom));
    T_REQUIRE(memory.hasPersistentBackup());
    T_EQ(memory.backupTypeName(), std::string("FLASH128"));

    // Enter ID mode: AA 55 90.
    memory.write8(0x0E005555U, 0xAAU);
    memory.write8(0x0E002AAAU, 0x55U);
    memory.write8(0x0E005555U, 0x90U);
    T_EQ(memory.read8(0x0E000000U), static_cast<gb::u8>(0x62U));
    T_EQ(memory.read8(0x0E000001U), static_cast<gb::u8>(0x13U));
    memory.write8(0x0E005555U, 0xF0U);

    // Program one byte: AA 55 A0 + value.
    memory.write8(0x0E005555U, 0xAAU);
    memory.write8(0x0E002AAAU, 0x55U);
    memory.write8(0x0E005555U, 0xA0U);
    memory.write8(0x0E000123U, 0x5AU);
    T_EQ(memory.read8(0x0E000123U), static_cast<gb::u8>(0x5AU));
}

TEST_CASE("cpu", "gba_backup_flash_defaults_to_erased_ff") {
    gb::gba::Memory memory;
    const auto rom = makeGbaRomWithBackupTag("FLASH1M_V102");
    T_REQUIRE(memory.loadRom(rom));

    T_EQ(memory.read8(0x0E000000U), static_cast<gb::u8>(0xFFU));
    T_EQ(memory.read8(0x0E000123U), static_cast<gb::u8>(0xFFU));
}

TEST_CASE("cpu", "gba_backup_flash_id_override_is_respected") {
    gb::gba::Memory memory;
    const auto rom = makeGbaRomWithBackupTag("FLASH_V121");
    T_REQUIRE(memory.loadRom(rom));

    memory.setFlashIdOverride(0x62, 0x13);
    memory.write8(0x0E005555U, 0xAAU);
    memory.write8(0x0E002AAAU, 0x55U);
    memory.write8(0x0E005555U, 0x90U);

    T_EQ(memory.read8(0x0E000000U), static_cast<gb::u8>(0x62U));
    T_EQ(memory.read8(0x0E000001U), static_cast<gb::u8>(0x13U));
}

TEST_CASE("cpu", "gba_backup_flash_compat_mode_uses_raw_byte_storage") {
    gb::gba::Memory memory;
    const auto rom = makeGbaRomWithBackupTag("FLASH_V121");
    T_REQUIRE(memory.loadRom(rom));
    memory.setFlashCompatibilityMode(true);
    T_REQUIRE(memory.flashCompatibilityMode());

    memory.write8(0x0E000055U, 0xA5U);
    T_EQ(memory.read8(0x0E000055U), static_cast<gb::u8>(0xA5U));
}

TEST_CASE("cpu", "gba_backup_flash_zero_legacy_file_is_migrated_to_erased_state") {
    gb::gba::Memory memory;
    const auto rom = makeGbaRomWithBackupTag("FLASH_V121");
    T_REQUIRE(memory.loadRom(rom));

    const auto legacyPath = tests::makeTempPath("gba_flash_legacy_zero", ".sav");
    std::vector<gb::u8> legacy(65536U, 0x00U);
    T_REQUIRE(tests::writeBinaryFile(legacyPath, legacy));
    T_REQUIRE(memory.loadBackupFromFile(legacyPath.string()));
    T_EQ(memory.read8(0x0E000000U), static_cast<gb::u8>(0xFFU));

    const auto freshPath = tests::makeTempPath("gba_flash_fresh", ".sav");
    T_REQUIRE(memory.saveBackupToFile(freshPath.string()));
    const auto freshBytes = tests::readBinaryFile(freshPath);
    T_REQUIRE(!freshBytes.empty());
    T_EQ(freshBytes.front(), static_cast<gb::u8>(0xFFU));

    std::error_code ec;
    std::filesystem::remove(legacyPath, ec);
    std::filesystem::remove(freshPath, ec);
}

TEST_CASE("cpu", "gba_backup_sram_halfword_and_word_writes_use_low_byte_only") {
    gb::gba::Memory memory;
    const auto rom = makeGbaRomWithBackupTag("SRAM_V113");
    T_REQUIRE(memory.loadRom(rom));

    memory.write16(0x0E000010U, 0xBEEFU);
    T_EQ(memory.read8(0x0E000010U), static_cast<gb::u8>(0xEFU));
    T_EQ(memory.read8(0x0E000011U), static_cast<gb::u8>(0xFFU));

    memory.write32(0x0E000012U, 0x11223344U);
    T_EQ(memory.read8(0x0E000012U), static_cast<gb::u8>(0x44U));
    T_EQ(memory.read8(0x0E000013U), static_cast<gb::u8>(0xFFU));
}

TEST_CASE("cpu", "gba_backup_eeprom_read_write_word_via_serial_bits") {
    gb::gba::Memory memory;
    const auto rom = makeGbaRomWithBackupTag("EEPROM_V124");
    T_REQUIRE(memory.loadRom(rom));
    T_REQUIRE(memory.hasPersistentBackup());
    T_EQ(memory.backupTypeName(), std::string("EEPROM"));

    constexpr gb::u8 kAddress = 0x03U; // 6-bit mode
    const std::array<gb::u8, 8> word = {0x12U, 0x34U, 0x56U, 0x78U, 0x9AU, 0xBCU, 0xDEU, 0xF0U};

    // WRITE command (6-bit): 10 + addr + 64 data bits + stop.
    eepromWriteBit(memory, 1U);
    eepromWriteBit(memory, 0U);
    for (int bit = 5; bit >= 0; --bit) {
        eepromWriteBit(memory, static_cast<gb::u8>((kAddress >> bit) & 0x1U));
    }
    for (gb::u8 byte : word) {
        for (int bit = 7; bit >= 0; --bit) {
            eepromWriteBit(memory, static_cast<gb::u8>((byte >> bit) & 0x1U));
        }
    }
    eepromWriteBit(memory, 0U); // stop

    // READ command (6-bit): 11 + addr + stop.
    eepromWriteBit(memory, 1U);
    eepromWriteBit(memory, 1U);
    for (int bit = 5; bit >= 0; --bit) {
        eepromWriteBit(memory, static_cast<gb::u8>((kAddress >> bit) & 0x1U));
    }
    eepromWriteBit(memory, 0U); // stop

    // 4 dummy bits.
    for (int i = 0; i < 4; ++i) {
        T_EQ(eepromReadBit(memory), static_cast<gb::u8>(0U));
    }
    for (gb::u8 byte : word) {
        gb::u8 rebuilt = 0U;
        for (int bit = 0; bit < 8; ++bit) {
            rebuilt = static_cast<gb::u8>((rebuilt << 1U) | eepromReadBit(memory));
        }
        T_EQ(rebuilt, byte);
    }
}

TEST_CASE("cpu", "gba_backup_persists_to_file_and_reload") {
    const auto rom = makeGbaRomWithBackupTag("SRAM_V113");
    gb::gba::Memory first;
    T_REQUIRE(first.loadRom(rom));
    first.write8(0x0E000044U, 0xABU);

    const auto path = tests::makeTempPath("gba_backup", ".sav");
    T_REQUIRE(first.saveBackupToFile(path.string()));

    gb::gba::Memory second;
    T_REQUIRE(second.loadRom(rom));
    T_REQUIRE(second.loadBackupFromFile(path.string()));
    T_EQ(second.read8(0x0E000044U), static_cast<gb::u8>(0xABU));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("cpu", "gba_backup_eeprom_dma_command_length_drives_auto_mode") {
    gb::gba::Memory memory;
    const auto rom = makeGbaRomWithBackupTag("EEPROM_V124");
    T_REQUIRE(memory.loadRom(rom));

    const auto writeBitsToEwram = [&](gb::u32 base, const std::vector<gb::u8>& bits) {
        for (std::size_t i = 0; i < bits.size(); ++i) {
            memory.write16(base + static_cast<gb::u32>(i * 2U), static_cast<gb::u16>(bits[i] & 0x1U));
        }
    };
    const auto runDma3 = [&](gb::u32 src, gb::u32 dst, gb::u16 count, gb::u16 control) {
        memory.write32(0x040000D4U, src);
        memory.write32(0x040000D8U, dst);
        memory.write16(0x040000DCU, count);
        memory.write16(0x040000DEU, control);
    };

    constexpr gb::u16 kDma16Enable = 0x8000U;
    constexpr gb::u16 kDstFixed = static_cast<gb::u16>(2U << 5U);
    constexpr gb::u16 kSrcFixed = static_cast<gb::u16>(2U << 7U);

    // WRITE (6-bit): 10 + addr + 64 data + stop => 73 bits.
    std::vector<gb::u8> writeCmd;
    writeCmd.reserve(73U);
    writeCmd.push_back(1U);
    writeCmd.push_back(0U);
    constexpr gb::u8 addr = 0x05U;
    for (int bit = 5; bit >= 0; --bit) {
        writeCmd.push_back(static_cast<gb::u8>((addr >> bit) & 0x1U));
    }
    const std::array<gb::u8, 8> word = {0xA1U, 0xB2U, 0xC3U, 0xD4U, 0xE5U, 0xF6U, 0x07U, 0x18U};
    for (gb::u8 byte : word) {
        for (int bit = 7; bit >= 0; --bit) {
            writeCmd.push_back(static_cast<gb::u8>((byte >> bit) & 0x1U));
        }
    }
    writeCmd.push_back(0U);
    writeBitsToEwram(0x02000000U, writeCmd);
    runDma3(0x02000000U, 0x0D000000U, static_cast<gb::u16>(writeCmd.size()), static_cast<gb::u16>(kDma16Enable | kDstFixed));

    // READ cmd (6-bit): 11 + addr + stop => 9 bits.
    std::vector<gb::u8> readCmd;
    readCmd.reserve(9U);
    readCmd.push_back(1U);
    readCmd.push_back(1U);
    for (int bit = 5; bit >= 0; --bit) {
        readCmd.push_back(static_cast<gb::u8>((addr >> bit) & 0x1U));
    }
    readCmd.push_back(0U);
    writeBitsToEwram(0x02001000U, readCmd);
    runDma3(0x02001000U, 0x0D000000U, static_cast<gb::u16>(readCmd.size()), static_cast<gb::u16>(kDma16Enable | kDstFixed));

    // Read back 68 halfwords (4 dummy + 64 data bits) via DMA source fixed.
    runDma3(0x0D000000U, 0x02002000U, 68U, static_cast<gb::u16>(kDma16Enable | kSrcFixed));
    for (int i = 0; i < 4; ++i) {
        T_EQ(static_cast<gb::u16>(memory.read16(0x02002000U + static_cast<gb::u32>(i * 2)) & 0x1U), static_cast<gb::u16>(0U));
    }

    for (std::size_t byteIndex = 0; byteIndex < word.size(); ++byteIndex) {
        gb::u8 rebuilt = 0U;
        for (int bit = 0; bit < 8; ++bit) {
            const gb::u32 offset = 0x02002000U + static_cast<gb::u32>((4 + byteIndex * 8U + static_cast<std::size_t>(bit)) * 2U);
            rebuilt = static_cast<gb::u8>((rebuilt << 1U) | static_cast<gb::u8>(memory.read16(offset) & 0x1U));
        }
        T_EQ(rebuilt, word[byteIndex]);
    }
}

TEST_CASE("cpu", "gba_backup_eeprom_strict_size_migrates_legacy_file") {
    const auto rom = makeGbaRomWithBackupTag("EEPROM_V124");
    gb::gba::Memory memory;
    T_REQUIRE(memory.loadRom(rom));
    memory.configureBackupBehavior(6, true); // EEPROM 4Kbit => 512 bytes strict

    const auto legacyPath = tests::makeTempPath("gba_eeprom_legacy", ".sav");
    std::vector<gb::u8> legacy(8192U, 0xAAU);
    T_REQUIRE(tests::writeBinaryFile(legacyPath, legacy));
    // Em modo dinamico, EEPROM legado pode ser migrado em vez de rejeitado.
    T_REQUIRE(memory.loadBackupFromFile(legacyPath.string()));

    const auto freshPath = tests::makeTempPath("gba_eeprom_fresh", ".sav");
    T_REQUIRE(memory.saveBackupToFile(freshPath.string()));
    const auto freshBytes = tests::readBinaryFile(freshPath);
    T_EQ(freshBytes.size(), static_cast<std::size_t>(512U));

    std::error_code ec;
    std::filesystem::remove(legacyPath, ec);
    std::filesystem::remove(freshPath, ec);
}

TEST_CASE("cpu", "gba_backup_eeprom_dynamic_loaded_size_is_preserved_on_resave") {
    const auto rom = makeGbaRomWithBackupTag("EEPROM_V124");
    gb::gba::Memory memory;
    T_REQUIRE(memory.loadRom(rom));

    const auto legacyPath = tests::makeTempPath("gba_eeprom_dynamic_legacy", ".sav");
    std::vector<gb::u8> legacy(8192U, 0xAAU);
    T_REQUIRE(tests::writeBinaryFile(legacyPath, legacy));
    T_REQUIRE(memory.loadBackupFromFile(legacyPath.string()));

    const auto freshPath = tests::makeTempPath("gba_eeprom_dynamic_fresh", ".sav");
    T_REQUIRE(memory.saveBackupToFile(freshPath.string()));
    const auto freshBytes = tests::readBinaryFile(freshPath);
    T_EQ(freshBytes.size(), static_cast<std::size_t>(8192U));

    std::error_code ec;
    std::filesystem::remove(legacyPath, ec);
    std::filesystem::remove(freshPath, ec);
}

TEST_CASE("cpu", "gba_system_super_mario_advance_2_rejects_legacy_512_byte_save") {
    const auto rom = makeGbaRomWithBackupTag("EEPROM_V124", "AA2E", "SUPERMARIOB");
    const auto romPath = tests::makeTempPath("gba_aa2e_profile", ".gba");
    T_REQUIRE(tests::writeBinaryFile(romPath, rom));

    const auto legacySavePath = tests::makeTempPath("gba_aa2e_legacy", ".sav");
    std::vector<gb::u8> legacy(512U, 0xAAU);
    T_REQUIRE(tests::writeBinaryFile(legacySavePath, legacy));

    gb::gba::System system;
    T_REQUIRE(system.loadRomFromFile(romPath.string()));
    T_EQ(system.compatibilityProfile().forcedEepromAddressBits, 14);
    T_EQ(system.memory().expectedBackupFileSize(), static_cast<std::size_t>(8192U));
    T_REQUIRE(!system.loadBackupFromFile(legacySavePath.string()));

    std::error_code ec;
    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(legacySavePath, ec);
}

TEST_CASE("cpu", "gba_system_advance_wars_uses_sanyo_flash64_id") {
    const auto rom = makeGbaRomWithBackupTag("FLASH512_V131", "AWRE", "ADVANCEWARS");
    const auto romPath = tests::makeTempPath("gba_awre_profile", ".gba");
    T_REQUIRE(tests::writeBinaryFile(romPath, rom));

    gb::gba::System system;
    T_REQUIRE(system.loadRomFromFile(romPath.string()));
    T_EQ(system.backupTypeName(), std::string("FLASH64"));
    T_EQ(system.compatibilityProfile().flashVendorId, 0x62);
    T_EQ(system.compatibilityProfile().flashDeviceId, 0x13);

    auto& memory = system.memory();
    memory.write8(0x0E005555U, 0xAAU);
    memory.write8(0x0E002AAAU, 0x55U);
    memory.write8(0x0E005555U, 0x90U);
    T_EQ(memory.read8(0x0E000000U), static_cast<gb::u8>(0x62U));
    T_EQ(memory.read8(0x0E000001U), static_cast<gb::u8>(0x13U));

    std::error_code ec;
    std::filesystem::remove(romPath, ec);
}

TEST_CASE("state", "gba_system_runframe_ends_on_frame_boundary") {
    const auto romPath = tests::makeTempPath("gba_frame_boundary", ".gba");
    tests::ScopedPath cleanupRom(romPath);

    const auto image = makeArmRom({
        0xEAFFFFFEU, // B .
    });
    T_REQUIRE(tests::writeBinaryFile(romPath, image));

    gb::gba::System system;
    T_REQUIRE(system.loadRomFromFile(romPath.string()));
    system.runFrame();

    T_EQ(system.memory().readIo16(gb::gba::Ppu::VcountOffset), static_cast<gb::u16>(0U));
    T_REQUIRE((system.memory().readIo16(gb::gba::Ppu::DispstatOffset) & 0x0003U) == 0U);
}

TEST_CASE("state", "gba_smoke_super_mario_advance_2_local_rom_boots_to_visible_frame") {
    const auto romPath = localRomPath(std::filesystem::path("roms") / "Super Mario Advance" / "Super Mario Advance 2 - Super Mario World (USA).gba");
    if (!std::filesystem::exists(romPath)) {
        return;
    }

    gb::gba::System system;
    T_REQUIRE(system.loadRomFromFile(romPath.string()));
    for (int i = 0; i < 120; ++i) {
        system.runFrame();
    }

    T_EQ(system.metadata().gameCode, std::string("AA2E"));
    T_REQUIRE(system.memory().readIo16(gb::gba::Ppu::DispcntOffset) != 0U);
    T_REQUIRE(countDistinctColors(system.framebuffer()) >= 2U);
}

TEST_CASE("state", "gba_smoke_advance_wars_local_rom_boots_past_flash_probe") {
    const auto romPath = localRomPath(std::filesystem::path("roms") / "Advance Wars" / "Advance Wars (USA).gba");
    if (!std::filesystem::exists(romPath)) {
        return;
    }

    gb::gba::System system;
    T_REQUIRE(system.loadRomFromFile(romPath.string()));
    for (int i = 0; i < 300; ++i) {
        system.runFrame();
    }

    T_EQ(system.metadata().gameCode, std::string("AWRE"));
    T_REQUIRE(system.memory().readIo16(gb::gba::Ppu::DispcntOffset) != 0U);
    T_REQUIRE(countDistinctColors(system.framebuffer()) >= 16U);
}
