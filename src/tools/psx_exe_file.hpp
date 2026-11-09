#pragma once

#include <cstdint>
#include <vector>

#include "binary_file.hpp"
#include "mips_tools.hpp"

class PsxExeFile : public BinaryFile
{
private:
    static constexpr uint32_t HEADER_SIZE = 0x800;
    static constexpr uint32_t FILE_SIZE_INCREMENT = 0x800;

    uint32_t _initial_code_start_addr;
    std::vector<uint8_t> _prepended_bytes;

public:
    // Startup at 0x80083F90 points SP at 0x8001FFF0. The chest-text typewriter
    // (0x80046FB4) then takes 7336 bytes, and drawing one glyph nests a few
    // hundred more, so those frames write down to about 0x8001E100. The 0x800
    // prefix below only covers ordinary calls. inject_stack_cushion() keeps
    // every later injection under TEXT_STACK_FLOOR.
    static constexpr uint32_t TEXT_STACK_FLOOR = 0x8001E000;
    static constexpr uint32_t STACK_CUSHION = 0x3000;

    static_assert(0x80020000 - FILE_SIZE_INCREMENT - STACK_CUSHION <= TEXT_STACK_FLOOR);

    explicit PsxExeFile(const std::filesystem::path& input_path) : BinaryFile(input_path)
    {
        _initial_code_start_addr = this->get_long_le(0x18);

        // Ordinary frames grow down from 0x8001FFF0 into these bytes.
        _prepended_bytes.resize(FILE_SIZE_INCREMENT, 0x0000);
    }

    // Zero bytes under the stack, injected after the slot block when there is
    // one so 0x8001F780 stays put. Chest text used to save return addresses
    // through the remote item names, and the typewriter then froze on the
    // pointer byte that replaced the rest of the name.
    void inject_stack_cushion()
    {
        MipsCode cushion;
        cushion.resize(STACK_CUSHION, 0);
        inject_code(cushion);
    }

    void save_as(const std::filesystem::path& output_path) override
    {
        // First, update the PSX-EXE header to expand the file
        this->update_psx_header();

        // Once updated, we can save the file as usual
        BinaryFile::save_as(output_path);
    }

    // Address inject_code will return for a blob of this size. The blob can
    // embed absolute pointers, then be injected, and those pointers stay valid:
    // save() pads below the blob and shifts the load address by the same amount.
    [[nodiscard]] uint32_t next_inject_address(uint32_t code_size) const
    {
        return _initial_code_start_addr - static_cast<uint32_t>(_prepended_bytes.size() + code_size);
    }

    uint32_t inject_code(const MipsCode& code)
    {
        _prepended_bytes.insert(_prepended_bytes.begin(), code.begin(), code.end());
        uint32_t effective_addr = _initial_code_start_addr - _prepended_bytes.size();
        return effective_addr;
    }

    void set_code(uint32_t addr, const MipsCode& code)
    {
        this->set_bytes(addr, code);
    }

    void update_psx_header()
    {
        uint32_t expansion_size = 0;
        while(expansion_size < _prepended_bytes.size())
            expansion_size += FILE_SIZE_INCREMENT;

        // Prepend padding bytes to ensure the size expansion is a multiple of 0x800
        uint32_t padding_size = expansion_size - _prepended_bytes.size();
        std::vector<uint8_t> empty_bytes;
        empty_bytes.resize(padding_size, 0xFF);
        _prepended_bytes.insert(_prepended_bytes.begin(), empty_bytes.begin(), empty_bytes.end());

        // Actually insert the bytes inside the data
        this->insert(this->begin() + HEADER_SIZE, _prepended_bytes.begin(), _prepended_bytes.end());

        if(expansion_size > 0)
        {
            // Shift the address where code is loaded
            uint32_t new_code_loading_addr = _initial_code_start_addr - expansion_size;
            this->set_long_le(0x18, new_code_loading_addr);

            // Update the amount of bytes to load
            this->set_long_le(0x1C, this->size() - HEADER_SIZE);
        }
    }
};