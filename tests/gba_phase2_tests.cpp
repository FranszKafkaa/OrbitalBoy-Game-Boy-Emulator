#include <vector>

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
