// Standalone regression test for Game::Lua51Transpiler using redistributable
// fixtures in tests/fixtures/lua51 (independent code, no Darkspore assets).
//
// usage: lua51_transpiler_test <fixture_dir> [transpiled_output.lua]
// The optional output is executed by CI with LuaJIT and compared with
// transpiler_fixture.expected, which was produced by running the original
// bytecode on a float32 Lua 5.1.5 interpreter.
#include "../darkspore_server/source/game/lua51_transpiler.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
	int failures = 0;

	void Check(bool condition, const std::string& what) {
		if (!condition) {
			std::cerr << "FAIL: " << what << '\n';
			++failures;
		}
	}

	std::string ReadAll(const std::filesystem::path& path) {
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			std::cerr << "cannot read " << path.string() << '\n';
			std::exit(2);
		}
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "usage: lua51_transpiler_test <fixture_dir> [transpiled_output.lua]\n";
		return 2;
	}
	namespace T = Game::Lua51Transpiler;
	const std::filesystem::path dir = argv[1];
	const std::string fixture = ReadAll(dir / "transpiler_fixture.luac");
	const std::string closeFixture = ReadAll(dir / "close_fixture.luac");
	const std::string fixtureSource = ReadAll(dir / "transpiler_fixture.lua");

	// Darkspore's header: Lua 5.1, little endian, int/size_t/instruction 4 bytes, float32 numbers.
	Check(T::IsLua51Bytecode(fixture), "fixture uses the Darkspore Lua 5.1 bytecode header");
	Check(!T::IsLua51Bytecode(fixtureSource), "plain Lua source is not treated as bytecode");
	Check(static_cast<unsigned char>(fixture[10]) == 4, "fixture lua_Number is float32 like Darkspore");

	const auto transpiled = T::TranspileBytecode(fixture);
	Check(transpiled.has_value(), "fixture transpiles");
	if (transpiled) {
		// Multiple results must keep their exact count, including nil holes.
		Check(transpiled->find("__lua51_pack(") != std::string::npos, "multi-results are packed with an explicit count");
		Check(transpiled->find(".n)") != std::string::npos, "multi-results are unpacked up to their recorded count");
		Check(transpiled->find("unpack(_v1)") == std::string::npos, "no unbounded unpack of a multi-result table");
		Check(transpiled->find("#_v") == std::string::npos, "SETLIST does not rely on the border of a table with holes");
	}

	// OP_CLOSE gives each loop iteration a fresh upvalue; the flat translation must refuse it.
	Check(T::IsLua51Bytecode(closeFixture), "close fixture is bytecode");
	Check(!T::TranspileBytecode(closeFixture).has_value(), "chunks containing OP_CLOSE are refused, not mistranslated");

	// Truncated or corrupted inputs from disk must be rejected without crashing.
	for (size_t len = 0; len < fixture.size(); ++len) {
		if (T::TranspileBytecode(std::string_view(fixture).substr(0, len)).has_value()) {
			Check(false, "truncated bytecode of length " + std::to_string(len) + " is rejected");
			break;
		}
	}
	std::string bigEndian = fixture;
	bigEndian[6] = 0;
	Check(!T::IsLua51Bytecode(bigEndian), "big-endian bytecode is rejected");
	std::string lua52 = fixture;
	lua52[4] = 0x52;
	Check(!T::IsLua51Bytecode(lua52), "non-5.1 bytecode is rejected");

	// No Darkspore script content may be embedded in the transpiler.
	const std::string header = ReadAll(dir / ".." / ".." / ".." / "darkspore_server" / "source" / "game" / "lua51_transpiler.h");
	Check(header.find("R\"lua(") == std::string::npos, "transpiler embeds no raw Lua script literals");
	Check(header.find("nAbility_") == std::string::npos, "transpiler embeds no ability script namespaces");

	if (argc >= 3 && transpiled) {
		std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
		out << *transpiled;
		Check(out.good(), "transpiled fixture written");
	}

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "lua51_transpiler_test: all checks passed\n";
	return 0;
}
