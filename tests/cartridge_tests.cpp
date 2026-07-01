#include <filesystem>
#include <string>
#include <vector>

#include "gb/core/cartridge.hpp"

#include "test_framework.hpp"
#include "test_utils.hpp"

namespace {

void loadCartridge(gb::Cartridge& cart, const tests::RomSpec& spec, tests::ScopedPath& cleanup) {
    const auto romPath = tests::writeTempRom(spec);
    cleanup = tests::ScopedPath(romPath);

    T_REQUIRE(cart.loadFromFile(romPath.string()));
}

} // namespace

TEST_CASE("cartridge", "load_from_file_fails_for_missing_path") {
    gb::Cartridge cart;
    T_REQUIRE(!cart.loadFromFile("/tmp/this_path_should_not_exist_123456.gb"));
}

TEST_CASE("cartridge", "load_from_file_rejects_too_small_rom") {
    const auto path = tests::makeTempPath("cart_small", ".gb");
    tests::ScopedPath cleanup(path);
    T_REQUIRE(tests::writeBinaryFile(path, std::vector<gb::u8>(0x100, 0x00)));

    gb::Cartridge cart;
    T_REQUIRE(!cart.loadFromFile(path.string()));
}

TEST_CASE("cartridge", "title_type_and_loaded_path") {
    tests::RomSpec spec{};
    spec.name = "cart_title";
    spec.title = "HELLO";
    spec.cartridgeType = 0x00;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    T_EQ(cart.title(), std::string("HELLO"));
    T_EQ(cart.cartridgeType(), 0x00);
    T_REQUIRE(!cart.loadedPath().empty());
}

TEST_CASE("cartridge", "cgb_flags_detection") {
    tests::RomSpec spec{};
    spec.name = "cart_cgb_flags";
    spec.cgbFlag = 0xC0;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    T_REQUIRE(cart.cgbSupported());
    T_REQUIRE(cart.cgbOnly());
    T_REQUIRE(cart.shouldRunInCgbMode());
}

TEST_CASE("cartridge", "nombc_reads_from_rom") {
    tests::RomSpec spec{};
    spec.name = "cart_nombc_rom";
    spec.fillBanksWithIndex = true;
    spec.romBanks = 2;
    spec.cartridgeType = 0x00;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    T_EQ(cart.read(0x0150), 0x00);
    T_EQ(cart.read(0x4000), 0x01);
}

TEST_CASE("cartridge", "nombc_ram_read_write_when_present") {
    tests::RomSpec spec{};
    spec.name = "cart_nombc_ram";
    spec.cartridgeType = 0x08;
    spec.ramCode = 0x02;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    T_REQUIRE(cart.hasRam());
    cart.write(0xA000, 0x5A);
    T_EQ(cart.read(0xA000), 0x5A);
}

TEST_CASE("cartridge", "battery_backed_ram_detection") {
    tests::RomSpec withBattery{};
    withBattery.name = "cart_battery_yes";
    withBattery.cartridgeType = 0x03;
    withBattery.ramCode = 0x03;

    tests::ScopedPath cleanupBattery;
    gb::Cartridge cartBattery;
    loadCartridge(cartBattery, withBattery, cleanupBattery);
    T_REQUIRE(cartBattery.hasBatteryBackedRam());

    tests::RomSpec withoutBattery{};
    withoutBattery.name = "cart_battery_no";
    withoutBattery.cartridgeType = 0x01;
    withoutBattery.ramCode = 0x03;

    tests::ScopedPath cleanupNoBattery;
    gb::Cartridge cartNoBattery;
    loadCartridge(cartNoBattery, withoutBattery, cleanupNoBattery);
    T_REQUIRE(!cartNoBattery.hasBatteryBackedRam());
}

TEST_CASE("cartridge", "mbc1_switches_rom_bank") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc1_rom_bank";
    spec.cartridgeType = 0x01;
    spec.romBanks = 4;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    T_EQ(cart.read(0x4000), 0x01);
    cart.write(0x2000, 0x02);
    T_EQ(cart.read(0x4000), 0x02);
}

TEST_CASE("cartridge", "mbc1_bank_zero_maps_to_one") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc1_bank_zero";
    spec.cartridgeType = 0x01;
    spec.romBanks = 4;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x2000, 0x00);
    T_EQ(cart.read(0x4000), 0x01);
}

TEST_CASE("cartridge", "mbc1_requires_ram_enable") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc1_ram_enable";
    spec.cartridgeType = 0x03;
    spec.ramCode = 0x03;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0xA000, 0xA5);
    T_EQ(cart.read(0xA000), 0xFF);

    cart.write(0x0000, 0x0A);
    cart.write(0xA000, 0xA5);
    T_EQ(cart.read(0xA000), 0xA5);
}

TEST_CASE("cartridge", "mbc1_ram_bank_mode") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc1_ram_bank";
    spec.cartridgeType = 0x03;
    spec.ramCode = 0x03;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x0000, 0x0A);
    cart.write(0x6000, 0x01);

    cart.write(0x4000, 0x00);
    cart.write(0xA000, 0x11);

    cart.write(0x4000, 0x01);
    cart.write(0xA000, 0x22);

    cart.write(0x4000, 0x00);
    T_EQ(cart.read(0xA000), 0x11);

    cart.write(0x4000, 0x01);
    T_EQ(cart.read(0xA000), 0x22);
}

TEST_CASE("cartridge", "mmm01_switches_rom_bank_after_mapping_lock") {
    tests::RomSpec spec{};
    spec.name = "cart_mmm01_rom_bank";
    spec.cartridgeType = 0x0B;
    spec.romBanks = 8;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    // Antes do lock, escrita em 0x2000 ajusta base.
    cart.write(0x2000, 0x02);
    T_EQ(cart.read(0x0000), 0x02);

    // Lock de mapeamento via RAM enable.
    cart.write(0x0000, 0x0A);
    cart.write(0x2000, 0x03);
    T_EQ(cart.read(0x4000), 0x05);
}

TEST_CASE("cartridge", "mbc5_switches_rom_bank") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc5_rom_bank";
    spec.cartridgeType = 0x19;
    spec.romBanks = 4;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x2000, 0x03);
    T_EQ(cart.read(0x4000), 0x03);
}

TEST_CASE("cartridge", "mbc5_ram_bank_switch") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc5_ram_bank";
    spec.cartridgeType = 0x1B;
    spec.ramCode = 0x03;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x0000, 0x0A);

    cart.write(0x4000, 0x00);
    cart.write(0xA000, 0x33);

    cart.write(0x4000, 0x01);
    cart.write(0xA000, 0x44);

    cart.write(0x4000, 0x00);
    T_EQ(cart.read(0xA000), 0x33);

    cart.write(0x4000, 0x01);
    T_EQ(cart.read(0xA000), 0x44);
}

TEST_CASE("cartridge", "huc3_virtual_command_register_roundtrip") {
    tests::RomSpec spec{};
    spec.name = "cart_huc3_virtual_cmd";
    spec.cartridgeType = 0xFE;
    spec.ramCode = 0x03;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x0000, 0x1A); // modo HuC3 comando 0x0A
    cart.write(0xA000, 0x0D);
    cart.write(0x0000, 0x1B); // comando 0x0B (read)
    T_EQ(cart.read(0xA000), static_cast<gb::u8>(0x0D));

    const auto saved = cart.state();
    cart.write(0xA000, 0x01);
    cart.loadState(saved);
    T_EQ(cart.read(0xA000), static_cast<gb::u8>(0x0D));
}

TEST_CASE("cartridge", "state_roundtrip_restores_mapper_and_ram") {
    tests::RomSpec spec{};
    spec.name = "cart_state_roundtrip";
    spec.cartridgeType = 0x1B;
    spec.ramCode = 0x03;
    spec.romBanks = 4;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x0000, 0x0A);
    cart.write(0x2000, 0x02);
    cart.write(0x4000, 0x01);
    cart.write(0xA000, 0x5C);

    const auto saved = cart.state();

    cart.write(0x2000, 0x03);
    cart.write(0x4000, 0x00);
    cart.write(0xA000, 0x1A);

    cart.loadState(saved);

    T_EQ(cart.read(0x4000), 0x02);
    T_EQ(cart.read(0xA000), 0x5C);
}

TEST_CASE("cartridge", "load_state_ignores_mismatched_cartridge_type") {
    tests::RomSpec spec{};
    spec.name = "cart_state_type_guard";
    spec.cartridgeType = 0x08;
    spec.ramCode = 0x02;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0xA000, 0x12);

    auto badState = cart.state();
    badState.type = 0x03;
    badState.ram[0] = 0x99;

    cart.loadState(badState);
    T_EQ(cart.read(0xA000), 0x12);
}

TEST_CASE("cartridge", "save_and_load_ram_file_roundtrip") {
    tests::RomSpec spec{};
    spec.name = "cart_ram_file_roundtrip";
    spec.cartridgeType = 0x08;
    spec.ramCode = 0x02;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0xA000, 0xBE);

    const auto ramPath = tests::makeTempPath("cart_ram", ".sav");
    tests::ScopedPath cleanupRam(ramPath);

    T_REQUIRE(cart.saveRamToFile(ramPath.string()));

    tests::ScopedPath cleanup2;
    gb::Cartridge otherCart;
    loadCartridge(otherCart, spec, cleanup2);
    T_REQUIRE(otherCart.loadRamFromFile(ramPath.string()));
    T_EQ(otherCart.read(0xA000), 0xBE);
}

TEST_CASE("cartridge", "ram_file_ops_fail_without_ram") {
    tests::RomSpec spec{};
    spec.name = "cart_no_ram_file_ops";
    spec.cartridgeType = 0x00;
    spec.ramCode = 0x00;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    const auto ramPath = tests::makeTempPath("cart_no_ram", ".sav");
    tests::ScopedPath cleanupRam(ramPath);

    T_REQUIRE(!cart.saveRamToFile(ramPath.string()));
    T_REQUIRE(!cart.loadRamFromFile(ramPath.string()));
}

TEST_CASE("cartridge", "mbc2_internal_ram_uses_low_nibble_only") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc2_ram";
    spec.cartridgeType = 0x06;
    spec.ramCode = 0x00;
    spec.romBanks = 4;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x0000, 0x0A); // RAM enable (A8=0)
    cart.write(0x2100, 0x03); // ROM bank (A8=1)
    T_EQ(cart.read(0x4000), 0x03);

    cart.write(0xA000, 0xAB);
    T_EQ(cart.read(0xA000), 0xFB);
}

TEST_CASE("cartridge", "mbc3_switches_rom_and_ram_banks") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc3_banks";
    spec.cartridgeType = 0x13;
    spec.ramCode = 0x03;
    spec.romBanks = 8;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x2000, 0x05);
    T_EQ(cart.read(0x4000), 0x05);

    cart.write(0x0000, 0x0A);
    cart.write(0x4000, 0x00);
    cart.write(0xA000, 0x12);
    cart.write(0x4000, 0x01);
    cart.write(0xA000, 0x34);
    cart.write(0x4000, 0x00);
    T_EQ(cart.read(0xA000), 0x12);
    cart.write(0x4000, 0x01);
    T_EQ(cart.read(0xA000), 0x34);
}

TEST_CASE("cartridge", "mbc3_rtc_register_read_write") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc3_rtc";
    spec.cartridgeType = 0x10;
    spec.ramCode = 0x03;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x0000, 0x0A);

    cart.write(0x4000, 0x08); // RTC seconds
    cart.write(0xA000, 0x2A);
    cart.write(0x6000, 0x00);
    cart.write(0x6000, 0x01); // latch
    T_EQ(cart.read(0xA000), 0x2A);

    cart.write(0x4000, 0x0C); // RTC day high
    cart.write(0xA000, 0xC1);
    cart.write(0x6000, 0x00);
    cart.write(0x6000, 0x01); // latch
    T_EQ(cart.read(0xA000), 0xC1);
}

TEST_CASE("cartridge", "huc1_uses_mbc1_style_banking") {
    tests::RomSpec spec{};
    spec.name = "cart_huc1";
    spec.cartridgeType = 0xFF;
    spec.romBanks = 4;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x2000, 0x02);
    T_EQ(cart.read(0x4000), 0x02);
}

TEST_CASE("cartridge", "huc3_uses_mbc3_style_banking") {
    tests::RomSpec spec{};
    spec.name = "cart_huc3";
    spec.cartridgeType = 0xFE;
    spec.romBanks = 4;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x2000, 0x03);
    T_EQ(cart.read(0x4000), 0x03);
}

TEST_CASE("cartridge", "rtc_detection_and_file_roundtrip_for_mbc3_timer") {
    tests::RomSpec spec{};
    spec.name = "cart_rtc_file_roundtrip";
    spec.cartridgeType = 0x10;
    spec.ramCode = 0x03;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);
    T_REQUIRE(cart.hasRtc());

    cart.write(0x0000, 0x0A);
    cart.write(0x4000, 0x08);
    cart.write(0xA000, 0x2A);
    cart.write(0x6000, 0x00);
    cart.write(0x6000, 0x01);
    T_EQ(cart.read(0xA000), 0x2A);

    const auto rtcPath = tests::makeTempPath("cart_rtc", ".rtc");
    tests::ScopedPath cleanupRtc(rtcPath);
    T_REQUIRE(cart.saveRtcToFile(rtcPath.string()));

    cart.write(0x4000, 0x08);
    cart.write(0xA000, 0x11);
    cart.write(0x6000, 0x00);
    cart.write(0x6000, 0x01);
    T_EQ(cart.read(0xA000), 0x11);

    T_REQUIRE(cart.loadRtcFromFile(rtcPath.string()));
    cart.write(0x6000, 0x00);
    cart.write(0x6000, 0x01);
    T_EQ(cart.read(0xA000), 0x2A);
}

TEST_CASE("cartridge", "rtc_file_ops_fail_without_timer_hardware") {
    tests::RomSpec spec{};
    spec.name = "cart_rtc_nohw";
    spec.cartridgeType = 0x13;
    spec.ramCode = 0x03;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);
    T_REQUIRE(!cart.hasRtc());

    const auto rtcPath = tests::makeTempPath("cart_no_rtc", ".rtc");
    tests::ScopedPath cleanupRtc(rtcPath);

    T_REQUIRE(!cart.saveRtcToFile(rtcPath.string()));
    T_REQUIRE(!cart.loadRtcFromFile(rtcPath.string()));
}

TEST_CASE("cartridge", "mmm01_uses_base_bank_before_mapping_lock") {
    tests::RomSpec spec{};
    spec.name = "cart_mmm01_alias";
    spec.cartridgeType = 0x0B;
    spec.romBanks = 4;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x2000, 0x02);
    // Antes do lock, o MMM01 aplica baseBank + romBankLow (default=1).
    T_EQ(cart.read(0x4000), 0x03);
}

TEST_CASE("cartridge", "mbc7_fallback_exposes_small_persistent_ram") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc7_alias";
    spec.cartridgeType = 0x22;
    spec.ramCode = 0x00;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    T_REQUIRE(cart.hasRam());
    T_REQUIRE(cart.hasBatteryBackedRam());

    cart.write(0x0000, 0x0A);
    cart.write(0xA000, 0x5A);
    T_EQ(cart.read(0xA000), 0x5A);
}

TEST_CASE("cartridge", "mbc7_sensor_register_window_read_write") {
    tests::RomSpec spec{};
    spec.name = "cart_mbc7_sensor";
    spec.cartridgeType = 0x22;
    spec.ramCode = 0x00;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x0000, 0x0A);
    cart.write(0xA000, 0x77);
    T_EQ(cart.read(0xA000), 0x77);
}

TEST_CASE("cartridge", "camera_fallback_uses_mbc5_like_banking") {
    tests::RomSpec spec{};
    spec.name = "cart_camera_alias";
    spec.cartridgeType = 0xFC;
    spec.romBanks = 8;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    T_REQUIRE(cart.hasRam());
    T_REQUIRE(cart.hasBatteryBackedRam());

    cart.write(0x2000, 0x05);
    T_EQ(cart.read(0x4000), 0x05);
}

TEST_CASE("cartridge", "camera_mapper_register_mode_and_ram_mode") {
    tests::RomSpec spec{};
    spec.name = "cart_camera_regs";
    spec.cartridgeType = 0xFC;
    spec.romBanks = 4;
    spec.fillBanksWithIndex = true;

    tests::ScopedPath cleanup;
    gb::Cartridge cart;
    loadCartridge(cart, spec, cleanup);

    cart.write(0x0000, 0x0A);

    // modo registrador camera
    cart.write(0x4000, 0x10);
    cart.write(0xA000, 0x66);
    T_EQ(cart.read(0xA000), 0x66);

    // modo RAM normal
    cart.write(0x4000, 0x00);
    cart.write(0xA000, 0x33);
    T_EQ(cart.read(0xA000), 0x33);
}
