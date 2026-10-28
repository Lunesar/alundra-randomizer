#pragma once

#include <cstdint>
#include <optional>
#include <string>

class PsxExeFile;

/**
 * Slot block embedded in a patched USA 1.1 executable.
 *
 * The bytes are the first inject_code on the file, so they stay at one address
 * while later injections land below them. MainRAM offset is the address with
 * the top bits dropped (0x8001F780 -> 0x1F780). The BizHawk client reads that
 * offset next to the TryIncrease prologue.
 *
 *   0x00  "ALAP"                 4 bytes
 *   0x04  block version         u16, currently 1
 *   0x06  reserved              u16, zero
 *   0x08  seed                  u32, the placement seed
 *   0x0C  randomizer version    16 bytes, NUL-terminated
 *   0x1C  apworld version       16 bytes, NUL-terminated
 *   0x2C  player                64 bytes, NUL-terminated login name
 *   0x6C  padding               zeros through 0x80
 *
 * write_slot_block must run before any other inject_code. A file that names
 * only one of apworld version and player is rejected. A file that names
 * neither is left without a block.
 */
namespace slot_block
{
    inline constexpr uint32_t USA_LOAD = 0x80020000;
    inline constexpr uint32_t RESERVED_PREFIX = 0x800;
    inline constexpr uint32_t SLOT_BLOCK_SIZE = 0x80;
    inline constexpr uint32_t SLOT_BLOCK_ADDRESS = USA_LOAD - RESERVED_PREFIX - SLOT_BLOCK_SIZE;
    inline constexpr uint32_t SLOT_BLOCK_MAIN_RAM = SLOT_BLOCK_ADDRESS & 0x001FFFFF;
    inline constexpr uint16_t BLOCK_VERSION = 1;
    inline constexpr std::size_t VERSION_FIELD = 16;
    inline constexpr std::size_t PLAYER_FIELD = 64;

    static_assert(SLOT_BLOCK_ADDRESS == 0x8001F780);
    static_assert(SLOT_BLOCK_MAIN_RAM == 0x1F780);

    struct Identity
    {
        std::string apworld_version;
        std::string player;
        uint32_t seed = 0;
    };

    std::optional<Identity> identity_for(const std::string& apworld_version, const std::string& player, uint32_t seed);
    void write(PsxExeFile& exe, const Identity& identity);
}
