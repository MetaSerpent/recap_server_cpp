//
// Created by vitor on 8/4/25.
//

#include "installer.h"
#include "game/lua51_transpiler.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

bool Installer::running = false;
std::string Installer::label = "";

namespace {
	// Every script is converted mechanically from the game's own Lua 5.1 bytecode.
	// No script content is shipped with or patched by the server. unluac is only a
	// fallback for chunks the transpiler refuses, and those are reported.
	void TranspileServerDataDirectory(const std::filesystem::path& serverDataRoot, const std::filesystem::path& outputRoot, const std::string& unluac) {
		if (!std::filesystem::exists(serverDataRoot)) {
			return;
		}
		size_t transpiled = 0;
		size_t decompiled = 0;
		for (auto& entry : std::filesystem::recursive_directory_iterator(serverDataRoot)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".lua") {
				continue;
			}
			std::string relativePath = std::filesystem::relative(entry.path(), serverDataRoot).string();
			std::filesystem::path outputPath = outputRoot / relativePath;
			std::filesystem::create_directories(outputPath.parent_path());

			if (Game::Lua51Transpiler::TranspileFile(entry.path(), outputPath)) {
				++transpiled;
				continue;
			}
			std::cout << "Lua transpiler refused " << relativePath << "; falling back to unluac." << std::endl;
			std::string command = unluac + " \"" + entry.path().string() + "\" > \"" + outputPath.string() + "\"";
			Installer::RunCommand(command);
			++decompiled;
		}
		std::cout << "Lua scripts converted from bytecode: " << transpiled
		          << ", decompiled with unluac: " << decompiled << std::endl;
	}
}

void Installer::LoadDarksporeData(std::string darksporeInstallPath, std::string darksporeInstallVersion)
{
	const auto& serverDataVersionBinPath = "data/serverdata/version_bin.txt";
	if (std::filesystem::exists(serverDataVersionBinPath)) {
		const std::filesystem::path serverDataPkg = std::filesystem::path(darksporeInstallPath) / "Data" / "ServerData.package";
		if (!std::filesystem::exists("data/serverdata/Modifiers") && std::filesystem::exists(serverDataPkg)) {
			label = "Updating Lua scripts and Modifiers from ServerData.package...";
			std::cout << "Updating Lua scripts and Modifiers from ServerData.package..." << std::endl;
#ifdef _WIN32
			// Explicit .\ so the tools resolve even when cmd.exe does not search the
			// current directory (NoDefaultCurrentDirectoryInExePath).
			std::string dbpf_unpacker = R"(.\dbpf_unpacker.exe)";
			std::string unluac = R"(.\unluac.exe)";
			RunCommand(dbpf_unpacker + " \"" + serverDataPkg.string() + "\" " + R"(ServerData\)");
#else
			std::string dbpf_unpacker = "./dbpf_unpacker";
			std::string unluac = "./unluac";
			RunCommand(dbpf_unpacker + " \"" + serverDataPkg.string() + "\" ./ServerData/");
#endif
			TranspileServerDataDirectory("ServerData", "ServerData_final", unluac);
			std::filesystem::remove_all("./ServerData");
			MergeDirectories("./ServerData_final/lua", "./data/serverdata/lua");
			MergeDirectories("./ServerData_final/Abilities", "./data/serverdata/Abilities");
			MergeDirectories("./ServerData_final/Modifiers", "./data/serverdata/Modifiers");
			MergeDirectories("./ServerData_final/behaviors", "./data/serverdata/behaviors");
			std::filesystem::remove_all("./ServerData_final");
		}
		label = "Server files are ready.";
		std::cout << "Server files are ready." << std::endl;
		return;
	}

	running = true;

#ifdef _WIN32
	std::string dbpf_unpacker = R"(.\dbpf_unpacker.exe)";
	std::string unluac        = R"(.\unluac.exe)";
	std::string recap_parser  = R"(.\recap_parser.exe)";

	// Step 1: Unpack packages
	std::cout << "Unpacking ServerData.package..." << std::endl;
	RunCommand(dbpf_unpacker + " \"" + darksporeInstallPath + R"(\Data\ServerData.package)" + "\" " + R"(ServerData\)");
	std::cout << "Unpacking AssetData_Binary.package..." << std::endl;
	RunCommand(dbpf_unpacker + " \"" + darksporeInstallPath + R"(\Data\AssetData_Binary.package)" + "\" " + R"(AssetData_Binary\)");

	// Step 2: Decompile .lua files
	std::cout << "Decompiling Lua scripts from ServerData..." << std::endl;
	TranspileServerDataDirectory("ServerData", "ServerData_final", unluac);

	// Step 3: Parse binary assets
	std::cout << "Parsing binary assets from AssetData_Binary..." << std::endl;
	RunCommand(recap_parser + " --recursive --silent --sort-ext --xml -o data\\serverdata AssetData_Binary");
#else
	std::string dbpf_unpacker = "./dbpf_unpacker";
	std::string unluac        = "./unluac";
	std::string recap_parser  = "./recap_parser";

	// Step 1: Unpack packages
	label = "Unpacking ServerData.package...";
	std::cout << "Unpacking ServerData.package..." << std::endl;
	RunCommand(dbpf_unpacker + " \"" + darksporeInstallPath + "/Data/ServerData.package\" ./ServerData/");
	label = "Unpacking AssetData_Binary.package...";
	std::cout << "Unpacking AssetData_Binary.package..." << std::endl;
	RunCommand(dbpf_unpacker + " \"" + darksporeInstallPath + "/Data/AssetData_Binary.package\" ./AssetData_Binary/");

	// Step 2: Decompile .lua files
	label = "Decompiling Lua scripts from ServerData...";
	std::cout << "Decompiling Lua scripts from ServerData..." << std::endl;
	TranspileServerDataDirectory("./ServerData", "./ServerData_final", unluac);

	// Step 3: Parse binary assets
	label = "Parsing binary assets from AssetData_Binary...";
	std::cout << "Parsing binary assets from AssetData_Binary..." << std::endl;
	RunCommand(recap_parser + " --recursive --silent --sort-ext --xml -o ./data/serverdata ./AssetData_Binary");
#endif

	// Step 4: Cleanup intermediate directories
	label = "Cleaning temporary folders...";
	std::cout << "Cleaning temporary folders..." << std::endl;
	std::filesystem::remove_all("./AssetData_Binary");
	std::filesystem::remove_all("./ServerData");

	// Step 5: Move final data into place
	label = "Moving new files to the correct place...";
	MergeDirectories("./ServerData_final/lua", "./data/serverdata/lua");
	MergeDirectories("./ServerData_final/Abilities", "./data/serverdata/Abilities");
	MergeDirectories("./ServerData_final/Modifiers", "./data/serverdata/Modifiers");
	MergeDirectories("./ServerData_final/behaviors", "./data/serverdata/behaviors");
	std::filesystem::remove_all("./ServerData_final");

	std::ofstream(serverDataVersionBinPath) << darksporeInstallVersion;

	running = false;
	label = "Server files are ready.";
	std::cout << "Server files are ready." << std::endl;
}

void Installer::RunCommand(const std::string& cmd) {
	std::filesystem::path log_path = std::filesystem::path("data") / "serverdata" / "debug.log";
	std::string log_cmd = cmd + " 2>&1";
	std::ofstream log(log_path, std::ios::app);
	if (log.is_open()) {
		log << log_cmd << std::endl;
	}

	int result = std::system(log_cmd.c_str());
	if (result != 0) {
		if (log.is_open()) {
			log << "Command failed: " << log_cmd << std::endl;
		}
		exit(result);
	}
}

void Installer::MergeDirectories(const std::filesystem::path& source, const std::filesystem::path& destination) {
	if (!std::filesystem::exists(source)) {
		return;
	}
	for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
		if (entry.is_directory()) continue;

		std::filesystem::path relativePath = std::filesystem::relative(entry.path(), source);
		std::filesystem::path destPath = destination / relativePath;

		if (!std::filesystem::exists(destPath)) {
			std::filesystem::create_directories(destPath.parent_path());
			std::filesystem::copy_file(entry.path(), destPath);
		}
	}
}