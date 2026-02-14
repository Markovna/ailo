#include "IrradianceMapGenerator.h"
#include <iostream>
#include <string>

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <command> [options]\n"
              << "\n"
              << "Commands:\n"
              << "  ibl-irradiance <input_path> <output_path> [output_size] [sample_count]\n"
              << "    Generate diffuse irradiance cubemap from equirectangular HDR environment map.\n"
              << "      input_path    Path to equirectangular HDR environment map\n"
              << "      output_path   Output path for cubemap faces (e.g. output.hdr)\n"
              << "                    Generates 6 files with suffixes: _px, _nx, _py, _ny, _pz, _nz\n"
              << "      output_size   Size of each cubemap face in pixels (default: 64)\n"
              << "      sample_count  Number of hemisphere samples for integration (default: 1024)\n"
              << "\n"
              << "  ibl-dfg <output_path>\n"
              << "    Generate DFG LUT texture for split-sum IBL approximation.\n"
              << "      output_path   Output path for DFG LUT (e.g. dfg.png)\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "ibl-irradiance") {
        if (argc < 4) {
            std::cerr << "Error: ibl-irradiance requires <input_path> and <output_path>\n\n";
            printUsage(argv[0]);
            return 1;
        }

        std::string inputPath = argv[2];
        std::string outputPath = argv[3];

        ailo::IrradianceGeneratorConfig config;
        if (argc >= 5) {
            config.outputSize = static_cast<uint32_t>(std::stoul(argv[4]));
        }
        if (argc >= 6) {
            config.sampleCount = static_cast<uint32_t>(std::stoul(argv[5]));
        }

        return ailo::IrradianceMapGenerator::irradiance(inputPath, outputPath, config) ? 0 : 1;

    } else if (command == "ibl-dfg") {
        if (argc < 3) {
            std::cerr << "Error: ibl-dfg requires <output_path>\n\n";
            printUsage(argv[0]);
            return 1;
        }

        std::string outputPath = argv[2];
        ailo::IrradianceMapGenerator::dfg(outputPath);
        return 0;

    } else {
        std::cerr << "Error: unknown command '" << command << "'\n\n";
        printUsage(argv[0]);
        return 1;
    }
}
