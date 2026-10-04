//////////////////////////////////////////////////////////////////////////////////////////
//
//     ALUNDRA RANDOMIZER
//
// ---------------------------------------------------------------------------------------
//
//     Developed by: Dinopony (@DinoponyRuns)
//
//////////////////////////////////////////////////////////////////////////////////////////

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

#ifndef WIN32
#include <sys/wait.h>
#endif

#include "personal_settings.hpp"
#include "randomizer_options.hpp"

#include "game/game_data.hpp"
#include "model/randomizer_world.hpp"
#include "world_shuffler.hpp"
#include "io/io.hpp"
#include "io/placement_seed_map.hpp"

#include "tools/argument_dictionary.hpp"
#include "tools/exception.hpp"
#include "tools/json.hpp"
#include "tools/base64.hpp"
#include "tools/binary_file.hpp"
#include "tools/psx_exe_file.hpp"
#include "tools/sha1.hpp"
#include "patches/patches.hpp"

namespace
{
    constexpr uint64_t ALUNDRA_USA_1_1_SIZE = 600359760;
    constexpr const char* ALUNDRA_USA_1_1_SHA1 = "26523fc6bd463890066ca81444217b4c10efb4e2";

    struct KnownUnsupportedImage
    {
        uint64_t size;
        const char* name;
    };

    // Redump sizes for known Alundra BIN images that this patcher does not support.
    constexpr KnownUnsupportedImage UNSUPPORTED_ALUNDRA_IMAGES[] = {
        { 600289200, "Alundra USA 1.0" },
        { 606013968, "Alundra Europe" },
        { 606206832, "Alundra France" },
        { 606049248, "Alundra Germany" },
        { 606110400, "Alundra Italy" },
        { 493049760, "Alundra Japan" },
        { 606119808, "Alundra Spain" },
    };

    // Filenames accepted when --input is omitted. input.bin stays first so existing setups keep working.
    constexpr const char* KNOWN_INPUT_IMAGE_NAMES[] = {
        "input.bin",
        "Alundra.bin",
        "Alundra (USA).bin",
        "Alundra (USA) (Rev 1).bin",
        "Alundra (USA) (v1.1).bin",
    };

    std::string format_known_input_image_names()
    {
        std::string result;
        bool first = true;
        for(const char* name : KNOWN_INPUT_IMAGE_NAMES)
        {
            if(!first)
                result += ", ";
            first = false;
            result += "'";
            result += name;
            result += "'";
        }
        return result;
    }

    std::filesystem::path resolve_input_image_path(const ArgumentDictionary& args)
    {
        const std::string input_arg = args.get_string("input");
        if(!input_arg.empty())
            return input_arg;

        for(const char* name : KNOWN_INPUT_IMAGE_NAMES)
        {
            std::filesystem::path candidate("./");
            candidate /= name;
            if(std::filesystem::is_regular_file(candidate))
                return candidate;
        }

        throw RandomizerException("Could not find an Alundra disc image in the randomizer folder. "
                                  "Please place your Alundra 1.1 US image there using one of these names: "
                                  + format_known_input_image_names()
                                  + ". You can also pass a path with --input=.");
    }

    bool requested_help(int argc, char* argv[], const ArgumentDictionary& args)
    {
        if(args.contains("help") || args.contains("h"))
            return true;

        for(int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if(arg == "-h" || arg == "-help" || arg == "/?")
                return true;
        }

        return false;
    }

    void print_help()
    {
        std::cout
            << "======== Alundra Randomizer v" << RELEASE << " ========\n"
            << "\n"
            << "Generate a randomized Alundra (USA 1.1) disc image.\n"
            << "\n"
            << "Usage: alundra-randomizer [options]\n"
            << "\n"
            << "Modes:\n"
            << "  (default)              Randomize, patch the disc, and write the spoiler log.\n"
            << "  --placement-out=PATH   Write a placement JSON and stop. No disc or hint log\n"
            << "                         (--outputrom and --outputlog are rejected).\n"
            << "  --placement-in=PATH    Patch the disc and write the spoiler log from that JSON.\n"
            << "\n"
            << "A .json or .bin PATH is a file. Any other PATH is a directory, and the file\n"
            << "is named after the hash sentence. That directory is created when its parent\n"
            << "already exists.\n"
            << "\n"
            << "Options:\n"
            << "  --help, -h             Show this help and exit.\n"
            << "  --input=PATH           Path to an Alundra USA 1.1 .bin image.\n"
            << "                         If omitted, the current folder is searched for:\n";
        for(const char* name : KNOWN_INPUT_IMAGE_NAMES)
            std::cout << "                           " << name << "\n";
        std::cout
            << "  --outputrom=PATH       Patched .bin (default: ./).\n"
            << "  --outputlog=PATH       Spoiler log .json (default: next to the output ROM).\n"
            << "  --preset=NAME          Preset in ./presets/ (prompted; Enter selects default).\n"
            << "  --permalink[=CODE]     Rebuild a seed. Prompts when CODE is omitted.\n"
            << "  --seedcount=N          How many seeds to generate (default: 1).\n"
            << "  --only-logic           Write the log only. No disc image or input ROM.\n"
            << "  --graph                Write ./logic.dot.\n"
            << "  --debuglog=PATH        Debug log JSON, when the preset allows spoilers.\n"
            << "  --verbose[=PATH]       Save dumpsxiso/mkpsxiso output (default: ./tool.log).\n"
#ifdef DEBUG
            << "  --dumpmodel            Dump the logic model to ./json_data/.\n"
#endif
            << "  --pause                Wait for Enter before exiting (the default).\n"
            << "  --nopause              Exit when finished.\n"
            << "\n"
            << "Item pool, starting inventory, and similar settings go in a preset, not flags.\n"
            << "\n"
            << "Examples:\n"
            << "  alundra-randomizer --preset=default --nopause\n"
            << "  alundra-randomizer --preset=default --verbose --nopause\n"
            << "  alundra-randomizer --input=\"Alundra (USA) (Rev 1).bin\" --outputrom=./seeds/\n"
            << "  alundra-randomizer --permalink --nopause\n"
            << "  alundra-randomizer --only-logic --preset=default --outputlog=./spoiler.json --nopause\n"
            << "  alundra-randomizer --preset=default --placement-out=./seeds/ --nopause\n"
            << "  alundra-randomizer --placement-in=./seeds/placement.json --nopause\n";
    }

    void validate_input_image(const std::filesystem::path& input_path)
    {
        if(!std::filesystem::exists(input_path))
        {
            throw RandomizerException("Input file '" + input_path.string() + "' was not found. "
                                      "Please place your Alundra 1.1 US disc image at that path, "
                                      "or omit --input to auto-detect a known filename ("
                                      + format_known_input_image_names() + ").");
        }

        const uint64_t file_size = std::filesystem::file_size(input_path);
        for(const KnownUnsupportedImage& image : UNSUPPORTED_ALUNDRA_IMAGES)
        {
            if(file_size == image.size)
            {
                throw RandomizerException("This image looks like " + std::string(image.name)
                                          + ", which is not supported. The randomizer requires Alundra USA 1.1.");
            }
        }

        if(file_size != ALUNDRA_USA_1_1_SIZE)
        {
            throw RandomizerException("Invalid file size (" + std::to_string(file_size) + ") on the image file. "
                                      "Make sure you are using a 1.1 US image.");
        }

        std::cout << "Verifying image checksum...\n";
        sha1::Hash digest;
        if(!sha1::hash_file(input_path, digest))
        {
            throw RandomizerException("Could not read image file '" + input_path.string()
                                      + "' while computing SHA-1 checksum.");
        }

        const std::string digest_hex = digest.hex();
        if(digest_hex != ALUNDRA_USA_1_1_SHA1)
        {
            throw RandomizerException("Image SHA-1 (" + digest_hex + ") does not match the known Alundra USA 1.1 dump ("
                                      + std::string(ALUNDRA_USA_1_1_SHA1) + "). "
                                      "The file may be corrupted or an unclean rip.");
        }
    }

#ifdef WIN32
    constexpr const char* DUMPSXISO_TOOL = "tools\\dumpsxiso.exe";
    constexpr const char* MKPSXISO_TOOL = "tools\\mkpsxiso.exe";
#else
    constexpr const char* DUMPSXISO_TOOL = "./tools/dumpsxiso";
    constexpr const char* MKPSXISO_TOOL = "./tools/mkpsxiso";
#endif

    void require_tool(const char* tool_path)
    {
        if(!std::filesystem::exists(tool_path))
        {
            throw RandomizerException("required tool " + std::string(tool_path)
                                      + " is missing; release packages ship it under tools/\n"
                                        "       If you built from source, get dumpsxiso and "
                                        "mkpsxiso from https://github.com/Lameguy64/mkpsxiso");
        }
    }

    void require_patch_tools()
    {
        require_tool(DUMPSXISO_TOOL);
        require_tool(MKPSXISO_TOOL);
    }

    std::string read_text_file(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        if(!in)
            return {};

        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    void print_captured_output(const std::string& captured)
    {
        if(captured.empty())
            return;

        std::cerr << captured;
        if(captured.back() != '\n')
            std::cerr << '\n';
    }

    constexpr const char* log_file = "./tool.log";

    std::filesystem::path resolve_tool_log_path(const ArgumentDictionary& args)
    {
        if(!args.get_boolean("verbose"))
            return {};

        const std::string path = args.get_string("verbose");
        std::filesystem::path resolved = (path.empty() || path == "true") ? log_file : std::filesystem::path(path);
        if(std::filesystem::is_directory(resolved))
            resolved /= "tool.log";
        return resolved;
    }

    std::filesystem::path resolve_capture_path(const std::filesystem::path& tool_log_path)
    {
        if(!tool_log_path.empty())
            return tool_log_path;
        return log_file;
    }

    void reset_tool_log(const std::filesystem::path& log_path)
    {
        std::ofstream out(log_path, std::ios::trunc);
        if(!out)
            throw RandomizerException("Could not open tool log file for writing at path '" + log_path.string() + "'");
    }

    void discard_capture_log(const std::filesystem::path& tool_log_path)
    {
        if(!tool_log_path.empty())
            return;

        std::error_code ec;
        std::filesystem::remove(resolve_capture_path({}), ec);
    }

    // Runs the command quietly. stdout/stderr append to one log file (./tool.log, or
    // --verbose=PATH). That file is kept on --verbose or if dump/rebuild fails.
    void run_external_command(const std::string& command,
                              const std::filesystem::path& tool_log_path = {},
                              const std::string& extra_failure_hint = "")
    {
        const std::filesystem::path capture_path = resolve_capture_path(tool_log_path);

        std::ofstream header(capture_path, std::ios::app);
        if(!header)
            throw RandomizerException("Could not open tool log file for writing at path '" + capture_path.string() + "'");
        header << ">>> " << command << "\n";
        header.close();

        const std::string redirected = command + " >> \"" + capture_path.string() + "\" 2>&1";
        const int status = std::system(redirected.c_str());
        if(status == -1)
        {
            print_captured_output(read_text_file(capture_path));
            throw RandomizerException("Could not launch command: " + command
                                      + " Tool output written to '" + capture_path.string() + "'.");
        }

#ifdef WIN32
        const bool success = (status == 0);
        const int code = status;
#else
        const bool exited = WIFEXITED(status);
        const int code = exited ? WEXITSTATUS(status) : status;
        const bool success = exited && code == 0;
#endif
        if(!success)
        {
            print_captured_output(read_text_file(capture_path));
            std::string message = "Command failed with code " + std::to_string(code) + ": " + command;
            if(!extra_failure_hint.empty())
                message += " " + extra_failure_hint;
            throw RandomizerException(message + " Tool output written to '" + capture_path.string() + "'.");
        }
    }

    enum class RandomizerRunMode
    {
        Full,
        GeneratePlacement,
        ApplyPlacement
    };

    RandomizerRunMode resolve_run_mode(const ArgumentDictionary& args)
    {
        const bool placement_out = args.contains("placement-out");
        const bool placement_in = args.contains("placement-in");
        if(placement_out && placement_in)
        {
            throw RandomizerException("--placement-out and --placement-in cannot be used together. "
                                      "Generate a placement seed map first, then apply it in a second run.");
        }

        if(placement_out)
        {
            if(args.get_string("placement-out").empty())
                throw RandomizerException("--placement-out requires a path to a .json file or a directory.");
            if(args.get_boolean("outputrom"))
            {
                throw RandomizerException("--outputrom cannot be used with --placement-out. "
                                          "This mode does not patch a disc image.");
            }
            if(args.get_boolean("outputlog"))
            {
                throw RandomizerException("--outputlog cannot be used with --placement-out. "
                                          "The hint log is written by --placement-in.");
            }
            return RandomizerRunMode::GeneratePlacement;
        }

        if(placement_in)
        {
            if(args.get_string("placement-in").empty())
                throw RandomizerException("--placement-in requires a path to a placement seed map .json file.");
            return RandomizerRunMode::ApplyPlacement;
        }

        return RandomizerRunMode::Full;
    }

    void reject_conflicting_placement_options(const ArgumentDictionary& args, const placement_seed_map::Contents& placement)
    {
        if(!placement.has_options)
            return;

        if(!args.get_string("preset").empty())
        {
            throw RandomizerException("This placement seed map already contains its settings. "
                                      "Remove --preset, or remove the permalink/settings from the file "
                                      "if you want the command line to supply them.");
        }

        const std::string cli_permalink = args.get_string("permalink");
        if(cli_permalink.empty())
            return;

        if(!placement.permalink.empty() && cli_permalink == placement.permalink)
            return;

        throw RandomizerException("This placement seed map already contains its settings. "
                                  "Remove --permalink, or pass the same permalink stored in the file.");
    }

    std::filesystem::path resolve_placement_output_path(const std::filesystem::path& requested, const std::string& hash_sentence)
    {
        if(requested.empty())
            throw RandomizerException("--placement-out requires a path to a .json file or a directory.");

        if(requested.extension() != ".json")
            return requested / (hash_sentence + ".json");

        return requested;
    }

    Json initial_spoiler_json(const RandomizerOptions& options, const GameData& game_data, const RandomizerWorld& world)
    {
        Json spoiler_json;
        spoiler_json["permalink"] = options.permalink();
        spoiler_json["hashSentence"] = options.hash_sentence();
        spoiler_json.merge_patch(options.to_json(game_data, world));
        return spoiler_json;
    }

    void write_output_log(const std::filesystem::path& spoiler_log_path, const Json& spoiler_json, bool allow_spoiler_log)
    {
        if(spoiler_log_path.empty())
            return;

        std::ofstream spoiler_file(spoiler_log_path);
        if(!spoiler_file)
            throw RandomizerException("Could not open output log file for writing at path '" + spoiler_log_path.string() + "'");

        spoiler_file << spoiler_json.dump(4);
        if(!spoiler_file)
            throw RandomizerException("Could not write output log file at path '" + spoiler_log_path.string() + "'");

        if(allow_spoiler_log)
            std::cout << "Spoiler log written into " << spoiler_log_path << ".\n";
        else
            std::cout << "Generation log written into " << spoiler_log_path << ".\n";
    }
}

/**
 * Calls the external tool `dumpsxiso` in order to dump the game image into a folder containing
 * all game files. A failed extract or missing output files are fatal.
 * 
 * @param input_file_path the path to the disc image file
 * @param output_dir_path the path to the output directory where game files will be extracted
 */
void dump_iso(const std::filesystem::path& input_file_path, const std::filesystem::path& output_dir_path,
              const std::filesystem::path& tool_log_path)
{
    const std::filesystem::path capture_path = resolve_capture_path(tool_log_path);
    reset_tool_log(capture_path);

    std::string command = DUMPSXISO_TOOL;
    command += " \"" + input_file_path.string() + "\"";
    command += " -x \"" + output_dir_path.string() + "\"";
    command += " -s \"" + output_dir_path.string() + "/build.xml\"";

    run_external_command(command, tool_log_path);

    const std::filesystem::path required_files[] = {
        output_dir_path / "DATA" / "DATAS.BIN",
        output_dir_path / "ALUN_CD.EXE",
        output_dir_path / "build.xml",
    };
    for(const std::filesystem::path& path : required_files)
    {
        if(!std::filesystem::exists(path))
        {
            print_captured_output(read_text_file(capture_path));
            throw RandomizerException("Required file '" + path.string()
                                      + "' was not created. See the extract log above. Tool output written to '"
                                      + capture_path.string() + "'.");
        }
    }
}

/**
 * Calls the external tool `mkpsxiso` in order to re-pack the game image from a folder containing
 * all game files. A non-zero exit is fatal.
 * 
 * @param input_dir_path the path to the directory containing game files
 * @param output_file_path the path to the output disc image file that will be created
 */
void rebuild_iso(const std::filesystem::path& input_dir_path, const std::filesystem::path& output_file_path,
                 const std::filesystem::path& tool_log_path)
{
    std::filesystem::path cue_file_path = output_file_path;
    cue_file_path.replace_extension("cue");

    std::string command = MKPSXISO_TOOL;
    command += " \"" + input_dir_path.string() + "build.xml\"";
    command += " -o \"" + output_file_path.string() + "\"";
    command += " -c \"" + cue_file_path.string() + "\"";
    command += " -y";

    run_external_command(command, tool_log_path, "The output image may currently be in use.");
}

namespace
{
    std::filesystem::path parent_or_current_directory(const std::filesystem::path& path)
    {
        const std::filesystem::path parent = path.parent_path();
        if(parent.empty())
            return ".";
        return parent;
    }

    // The directory above an output must already exist. This does not create it.
    void require_existing_directory(const std::filesystem::path& directory, const std::string& action)
    {
        std::error_code error;
        if(std::filesystem::is_directory(directory, error))
            return;
        if(std::filesystem::exists(directory, error))
        {
            throw RandomizerException(action + " because '" + directory.string() + "' is not a directory.");
        }
        throw RandomizerException(action + " because directory '" + directory.string() + "' does not exist.");
    }

    // Create only the last directory, and only when its parent already exists.
    // A path like "missing/new" must fail instead of creating both levels.
    void ensure_output_directory(const std::filesystem::path& directory)
    {
        std::error_code error;
        if(std::filesystem::is_directory(directory, error))
            return;
        if(std::filesystem::exists(directory, error))
            throw RandomizerException("Output path '" + directory.string() + "' is not a directory.");

        require_existing_directory(parent_or_current_directory(directory),
                                   "Cannot create output directory '" + directory.string() + "'");

        const bool created = std::filesystem::create_directory(directory, error);
        if(error)
        {
            throw RandomizerException("Could not create output directory '" + directory.string() + "': "
                                      + error.message());
        }
        if(created)
            std::cout << "Created output directory '" << directory.string() << "'.\n";
    }

    void ensure_output_file_parent(const std::filesystem::path& file_path)
    {
        require_existing_directory(parent_or_current_directory(file_path),
                                   "Cannot write '" + file_path.string() + "'");
    }
}

/**
 * Process the given output paths (input by the user) to alter them following a bunch of rules.
 * Paths that will actually be written are checked here, before randomization and disc patching.
 *
 * @param output_rom_path a reference on the path that will be used for the output ROM
 * @param spoiler_log_path a reference on the path that will be used for the spoiler log
 * @param hash_sentence the seed unique "hash sentence", used as a default filename if none was given
 * @param write_rom when false, the ROM path is not checked or created (logic-only runs)
 */
void process_paths(std::filesystem::path& output_rom_path, std::filesystem::path& spoiler_log_path,
                   const std::string& hash_sentence, bool write_rom)
{
    // If output ROM path was not specified, put it in the current working directory
    if(output_rom_path.empty())
        output_rom_path = "./";
    
    // If output log path wasn't specified, put it alongside the ROM
    if(spoiler_log_path.empty())
    {
        spoiler_log_path = output_rom_path;
        if(spoiler_log_path.has_extension())
            spoiler_log_path.replace_extension(".json");
    }

    // A path without the file extension is a directory. Create that one directory when its
    // parent already exists. A .bin/.json path must already have a parent directory.
    const bool rom_is_directory = output_rom_path.extension() != ".bin";
    const bool log_is_directory = spoiler_log_path.extension() != ".json";
    if(write_rom && rom_is_directory)
        ensure_output_directory(output_rom_path);
    if(log_is_directory && !(write_rom && rom_is_directory && output_rom_path == spoiler_log_path))
        ensure_output_directory(spoiler_log_path);
    if(write_rom && !rom_is_directory)
        ensure_output_file_parent(output_rom_path);
    if(!log_is_directory)
        ensure_output_file_parent(spoiler_log_path);

    // If path was not containing the appropriate file extension, it is considered as a directory path,
    // so append a default filename to it.
    if(rom_is_directory)
        output_rom_path = output_rom_path / (hash_sentence + ".bin");
    if(log_is_directory)
        spoiler_log_path = spoiler_log_path / (hash_sentence + ".json");
}

Json randomize(RandomizerWorld& world, GameData& game_data, RandomizerOptions& options, PersonalSettings& personal_settings, const ArgumentDictionary& args,
              bool randomize_hint_sources = true)
{
    Json spoiler_json = initial_spoiler_json(options, game_data, world);

    std::cout << "\nRandomizing world...\n";
    WorldShuffler shuffler(world, game_data, options);
    shuffler.randomize_items();
    if(randomize_hint_sources)
        shuffler.randomize_hints();

    if(options.allow_spoiler_log())
    {
        spoiler_json.merge_patch(SpoilerWriter::build_spoiler_json(world, options));
        spoiler_json["playthrough"] = shuffler.playthrough_as_json();

        // Output debug log if requested, only if spoiler log is authorized
        std::string debug_log_path = args.get_string("debuglog");
        if (!debug_log_path.empty())
        {
            std::ofstream debug_log_file(debug_log_path);
            debug_log_file << shuffler.debug_log_as_json().dump(4);
            debug_log_file.close();
        }

#ifdef DEBUG
        // Output model if requested, only if spoiler log is authorized
        if(args.get_boolean("dumpmodel"))
        {
            std::cout << "Outputting model...\n\n";
            ModelWriter::write_logic_model(world);
            std::cout << "Model dumped to './json_data/'" << std::endl;
        }
#endif
    }

    return spoiler_json;
}

void build_patched_rom(const std::filesystem::path& input_path, const std::filesystem::path& output_path, 
                       GameData& game_data, RandomizerWorld& world, const RandomizerOptions& options,
                       const std::filesystem::path& tool_log_path)
{
#ifdef DEBUG
    std::filesystem::remove_all("./tmp_dump/");
#endif

    std::cout << "Checking input image '" << input_path.string() << "'...\n";
    validate_input_image(input_path);

    // Dump the input ROM into a "tmp_dump" folder
    std::cout << "Extracting game files...\n";
    if(!tool_log_path.empty())
        std::cout << "Writing tool output to " << tool_log_path << ".\n";
    dump_iso(input_path, "./tmp_dump/", tool_log_path);

    // Apply patches to relevant files that were extracted from the game ROM
    std::cout << "Editing game files...\n";
    BinaryFile datas_file("./tmp_dump/DATA/DATAS.BIN");
    PsxExeFile exe_file("./tmp_dump/ALUN_CD.EXE");

    apply_randomizer_patches(datas_file, exe_file, game_data, world, options);

    datas_file.save();
    exe_file.save();

    // Use an external tool to repack the files into a PS1 disc image
    std::cout << "Building a disc image...\n";
    rebuild_iso("./tmp_dump/", output_path, tool_log_path);

#ifndef DEBUG
    std::filesystem::remove_all("./tmp_dump/");
#endif

    discard_capture_log(tool_log_path);

    std::cout << "Randomized game outputted to " << output_path << ".\n";
}

void generate(const ArgumentDictionary& args)
{
    const RandomizerRunMode mode = resolve_run_mode(args);

    placement_seed_map::Contents placement;
    if(mode == RandomizerRunMode::ApplyPlacement)
    {
        const std::filesystem::path placement_path = args.get_string("placement-in");
        std::cout << "Reading placement seed map '" << placement_path.string() << "'...\n";
        placement = placement_seed_map::read(placement_path);
        reject_conflicting_placement_options(args, placement);
    }

    // Fail fast if the disc image or patch tools are missing.
    // --only-logic and --placement-out need neither.
    const bool patch_rom = mode != RandomizerRunMode::GeneratePlacement && !args.contains("only-logic");
    std::filesystem::path input_rom_path;
    if(patch_rom)
    {
        require_patch_tools();
        input_rom_path = resolve_input_image_path(args);
        std::cout << "Using input image '" << input_rom_path.string() << "'.\n";
    }

    GameData game_data;
    RandomizerWorld world(game_data);
    const bool use_placement_options = mode == RandomizerRunMode::ApplyPlacement && placement.has_options;
    if(use_placement_options)
        std::cout << "Using settings from the placement seed map.\n";

    RandomizerOptions options = use_placement_options
        ? RandomizerOptions(placement.options_json, game_data, world)
        : RandomizerOptions(args, game_data, world);
    PersonalSettings personal_settings(args);

    game_data.apply_options(options);
    world.apply_options(options, game_data);

    // Reject a bad output path before randomization or disc patching.
    std::filesystem::path output_rom_path;
    std::filesystem::path spoiler_log_path;
    if(mode != RandomizerRunMode::GeneratePlacement)
    {
        output_rom_path = args.get_string("outputrom", "");
        spoiler_log_path = args.get_string("outputlog", "");
        process_paths(output_rom_path, spoiler_log_path, options.hash_sentence(), patch_rom);
    }

    Json spoiler_json;
    if(mode == RandomizerRunMode::ApplyPlacement)
    {
        std::cout << "\nApplying placement seed map (" << placement.placements.size() << " locations)...\n";
        placement_seed_map::apply(world, game_data, placement.placements);

        std::cout << "Generating hints...\n";
        WorldShuffler shuffler(world, game_data, options);
        shuffler.randomize_hints();

        spoiler_json = initial_spoiler_json(options, game_data, world);
        if(options.allow_spoiler_log())
            spoiler_json.merge_patch(SpoilerWriter::build_spoiler_json(world, options));
    }
    else
    {
        const bool randomize_hint_sources = mode == RandomizerRunMode::Full;
        spoiler_json = randomize(world, game_data, options, personal_settings, args, randomize_hint_sources);
    }

    if(mode == RandomizerRunMode::GeneratePlacement)
    {
        const std::filesystem::path placement_output = resolve_placement_output_path(
            args.get_string("placement-out"), options.hash_sentence());
        const std::filesystem::path placement_parent = placement_output.parent_path();
        if(!placement_parent.empty())
        {
            std::error_code directory_error;
            std::filesystem::create_directories(placement_parent, directory_error);
            if(directory_error)
            {
                throw RandomizerException("Could not create directory '" + placement_parent.string()
                                          + "' for the placement seed map: " + directory_error.message());
            }
        }

        placement_seed_map::write(placement_output, options, world);
        std::cout << "Placement seed map written into " << placement_output.string() << ".\n";
        std::cout << "No disc image was patched.\n";
    }
    else
    {
        // A settings-based placement file has no permalink of its own. The string
        // permalink() would build from those settings does not replay the placements.
        if(mode == RandomizerRunMode::ApplyPlacement && placement.permalink.empty())
            spoiler_json.erase("permalink");

        if(patch_rom)
            build_patched_rom(input_rom_path, output_rom_path, game_data, world, options,
                              resolve_tool_log_path(args));

        write_output_log(spoiler_log_path, spoiler_json, options.allow_spoiler_log());
    }

    if(args.contains("graph"))
        GraphvizWriter::write_logic_as_dot(world, "./logic.dot");

    const bool print_permalink = mode != RandomizerRunMode::ApplyPlacement || !placement.permalink.empty();

    std::cout << "\nHash sentence: " << options.hash_sentence() << "\n";
    if(print_permalink)
        std::cout << "\nPermalink: " << options.permalink() << "\n";
    if(mode == RandomizerRunMode::GeneratePlacement)
    {
        std::cout << "\nApply this placement seed map with --placement-in to patch the game and write the hint log.\n"
                  << std::endl;
    }
    else if(print_permalink)
    {
        std::cout << "\nShare the permalink above with other people to enable them building the exact same seed.\n"
                  << std::endl;
    }
}

int main(int argc, char* argv[])
{
    int return_code = EXIT_SUCCESS;

    ArgumentDictionary args(argc, argv);

    if(requested_help(argc, argv, args))
    {
        print_help();
        return EXIT_SUCCESS;
    }

    std::cout << "======== Alundra Randomizer v" << RELEASE << " ========\n\n";

    if(args.contains("permalink") && args.get_string("permalink").empty())
    {
        std::string permalink;
        std::cout << "Please specify a permalink: ";
        std::getline(std::cin, permalink);
        args.set_string("permalink", permalink);
    }

    try
    {
        const RandomizerRunMode mode = resolve_run_mode(args);
        int seed_count = args.get_integer("seedcount", 1);
        if(mode == RandomizerRunMode::ApplyPlacement && seed_count > 1)
        {
            throw RandomizerException("--seedcount cannot be greater than 1 with --placement-in. "
                                      "A placement seed map is a single seed.");
        }
        if(mode == RandomizerRunMode::GeneratePlacement && seed_count > 1)
        {
            const std::filesystem::path placement_path = args.get_string("placement-out");
            if(placement_path.extension() == ".json")
            {
                throw RandomizerException("--placement-out must be a directory when --seedcount is greater than 1.");
            }
        }

        for(int i=0 ; i<seed_count ; ++i)
            generate(args);
    }
    catch(RandomizerException& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return_code = EXIT_FAILURE;
    }
    catch(Json::parse_error& e)
    {
        std::cerr << "ERROR: Malformed json -> " << e.what() << std::endl;
        return_code = EXIT_FAILURE;
    }

    if(args.get_boolean("pause", true))
    {
        std::cout << "\nPress any key to exit.";
        std::string dummy;
        std::getline(std::cin, dummy);
    }

    return return_code;
}
