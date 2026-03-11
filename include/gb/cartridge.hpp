#pragma once

#include <memory>
#include <string>
#include <vector>

#include "gb/types.hpp"

namespace gb {

class Mapper {
public:
    virtual ~Mapper() = default;
    virtual u8 read(u16 address) const = 0;
    virtual void write(u16 address, u8 value) = 0;
    [[nodiscard]] virtual std::vector<u8> state() const = 0;
    virtual void loadState(const std::vector<u8>& state) = 0;
};

class Cartridge {
public:
    struct State {
        u8 type = 0;
        std::vector<u8> ram;
        std::vector<u8> mapper;
    };

    bool loadFromFile(const std::string& path);
    u8 read(u16 address) const;
    void write(u16 address, u8 value);

    [[nodiscard]] std::string title() const;
    [[nodiscard]] u8 cartridgeType() const;
    [[nodiscard]] State state() const;
    void loadState(const State& state);

private:
    std::vector<u8> rom_;
    std::vector<u8> ram_;
    std::unique_ptr<Mapper> mapper_;
};

} // namespace gb
