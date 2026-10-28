#include "slot_block.hpp"

#include <string>

#include "../tools/exception.hpp"
#include "../tools/mips_tools.hpp"
#include "../tools/psx_exe_file.hpp"

namespace
{
    void add_field(ByteArray& block, const std::string& text, std::size_t width, const char* field)
    {
        if(text.empty() || text.size() >= width || text.find('\0') != std::string::npos)
        {
            throw RandomizerException(std::string("Slot block field \"") + field + "\" does not fit.");
        }
        for(unsigned char character : text)
        {
            if(character < 0x20 || character == 0x7F)
                throw RandomizerException(std::string("Slot block field \"") + field + "\" contains a control character.");
        }

        const std::size_t start = block.size();
        block.add_bytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        while(block.size() < start + width)
            block.add_byte(0);
    }
}

std::optional<slot_block::Identity> slot_block::identity_for(const std::string& apworld_version,
                                                             const std::string& player, uint32_t seed)
{
    const bool has_player = !player.empty();
    const bool has_version = !apworld_version.empty();
    if(has_player != has_version)
    {
        throw RandomizerException(
            "Placement seed map must name both apworldVersion and player, or neither.");
    }
    if(!has_player)
        return std::nullopt;

    Identity identity;
    identity.apworld_version = apworld_version;
    identity.player = player;
    identity.seed = seed;
    return identity;
}

void slot_block::write(PsxExeFile& exe, const Identity& identity)
{
    // Later injections are inserted below this blob, so this address stays put
    // only when nothing has been injected yet.
    if(exe.next_inject_address(SLOT_BLOCK_SIZE) != SLOT_BLOCK_ADDRESS)
    {
        throw RandomizerException(
            "The slot block is not the first code injected into a USA 1.1 executable.");
    }

    ByteArray block;
    const uint8_t magic[] = { 'A', 'L', 'A', 'P' };
    block.add_bytes(magic, sizeof(magic));
    block.add_word_le(BLOCK_VERSION);
    block.add_word_le(0);
    block.add_long_le(identity.seed);
    add_field(block, RELEASE, VERSION_FIELD, "randomizer version");
    add_field(block, identity.apworld_version, VERSION_FIELD, "apworld version");
    add_field(block, identity.player, PLAYER_FIELD, "player");
    while(block.size() < SLOT_BLOCK_SIZE)
        block.add_byte(0);
    if(block.size() != SLOT_BLOCK_SIZE)
        throw RandomizerException("Slot block size changed.");

    MipsCode code;
    code.add_bytes(block);
    if(exe.inject_code(code) != SLOT_BLOCK_ADDRESS)
        throw RandomizerException("Slot block was injected at the wrong address.");
}
