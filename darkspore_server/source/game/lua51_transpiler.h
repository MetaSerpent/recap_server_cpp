#ifndef _GAME_LUA51_TRANSPILER_H
#define _GAME_LUA51_TRANSPILER_H

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Game::Lua51Transpiler {

	inline bool IsLuaKeyword(std::string_view s) {
		static constexpr std::string_view keywords[] = {
			"and", "break", "do", "else", "elseif", "end", "false", "for",
			"function", "goto", "if", "in", "local", "nil", "not", "or",
			"repeat", "return", "then", "true", "until", "while"
		};
		for (auto kw : keywords) {
			if (s == kw) {
				return true;
			}
		}
		return false;
	}

	inline bool IsIdentifier(std::string_view s) {
		if (s.empty()) {
			return false;
		}
		const unsigned char first = static_cast<unsigned char>(s.front());
		if (!(std::isalpha(first) || first == '_')) {
			return false;
		}
		for (unsigned char ch : s) {
			if (!(std::isalnum(ch) || ch == '_')) {
				return false;
			}
		}
		return !IsLuaKeyword(s);
	}

	inline std::string QuoteLuaString(std::string_view s) {
		std::string out;
		out.reserve(s.size() + 4);
		out.push_back('"');
		for (unsigned char c : s) {
			if (c == '"') {
				out += "\\\"";
			} else if (c == '\\') {
				out += "\\\\";
			} else if (c == '\n') {
				out += "\\n";
			} else if (c == '\r') {
				out += "\\r";
			} else if (c == '\t') {
				out += "\\t";
			} else if (c >= 32 && c < 127) {
				out.push_back(static_cast<char>(c));
			} else {
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\%03u", static_cast<unsigned int>(c));
				out += buf;
			}
		}
		out.push_back('"');
		return out;
	}

	struct ConstVal {
		uint8_t type = 0;
		bool bval = false;
		double nval = 0.0;
		std::string sval;
	};

	struct Proto {
		std::string source;
		int32_t linedef = 0;
		int32_t lastlinedef = 0;
		uint8_t nups = 0;
		uint8_t numparams = 0;
		uint8_t is_vararg = 0;
		uint8_t maxstack = 0;
		std::vector<uint32_t> code;
		std::vector<ConstVal> consts;
		std::vector<Proto> protos;
	};

	inline std::string FormatConst(const ConstVal& c) {
		if (c.type == 0) {
			return "nil";
		}
		if (c.type == 1) {
			return c.bval ? "true" : "false";
		}
		if (c.type == 3) {
			if (std::isnan(c.nval)) {
				return "(0/0)";
			}
			if (std::isinf(c.nval)) {
				return c.nval > 0 ? "math.huge" : "(-math.huge)";
			}
			const double rounded = std::trunc(c.nval);
			if (c.nval == rounded && std::fabs(c.nval) < 1e14) {
				return std::to_string(static_cast<long long>(rounded));
			}
			std::ostringstream oss;
			oss << std::setprecision(7) << c.nval;
			return oss.str();
		}
		if (c.type == 4) {
			return QuoteLuaString(c.sval);
		}
		return "nil";
	}

	inline bool IsLua51Bytecode(std::string_view data) {
		if (data.size() < 12) {
			return false;
		}
		if (data.substr(0, 4) != "\x1bLua") {
			return false;
		}
		const auto ver = static_cast<uint8_t>(data[4]);
		const auto fmt = static_cast<uint8_t>(data[5]);
		const auto endian = static_cast<uint8_t>(data[6]);
		const auto int_sz = static_cast<uint8_t>(data[7]);
		const auto size_t_sz = static_cast<uint8_t>(data[8]);
		const auto instr_sz = static_cast<uint8_t>(data[9]);
		const auto num_sz = static_cast<uint8_t>(data[10]);
		return ver == 0x51 && fmt == 0 && endian == 1 && int_sz == 4 &&
		       (size_t_sz == 4 || size_t_sz == 8) && instr_sz == 4 &&
		       (num_sz == 4 || num_sz == 8);
	}

	namespace Detail {

		struct Reader {
			std::string_view data;
			size_t pos = 12;
			uint8_t size_t_sz = 4;
			uint8_t num_sz = 4;
			bool ok = true;

			uint8_t ReadByte() {
				if (pos + 1 > data.size()) {
					ok = false;
					return 0;
				}
				return static_cast<uint8_t>(data[pos++]);
			}

			int32_t ReadInt() {
				if (pos + 4 > data.size()) {
					ok = false;
					return 0;
				}
				int32_t v = 0;
				std::memcpy(&v, data.data() + pos, 4);
				pos += 4;
				return v;
			}

			size_t ReadSizeT() {
				if (size_t_sz == 4) {
					if (pos + 4 > data.size()) {
						ok = false;
						return 0;
					}
					uint32_t v = 0;
					std::memcpy(&v, data.data() + pos, 4);
					pos += 4;
					return static_cast<size_t>(v);
				}
				if (pos + 8 > data.size()) {
					ok = false;
					return 0;
				}
				uint64_t v = 0;
				std::memcpy(&v, data.data() + pos, 8);
				pos += 8;
				return static_cast<size_t>(v);
			}

			std::string ReadString() {
				const size_t sz = ReadSizeT();
				if (!ok || sz == 0) {
					return {};
				}
				if (pos + sz > data.size()) {
					ok = false;
					return {};
				}
				std::string s(data.data() + pos, sz - 1);
				pos += sz;
				return s;
			}

			double ReadNumber() {
				if (num_sz == 4) {
					if (pos + 4 > data.size()) {
						ok = false;
						return 0.0;
					}
					float f = 0.0f;
					std::memcpy(&f, data.data() + pos, 4);
					pos += 4;
					return static_cast<double>(f);
				}
				if (pos + 8 > data.size()) {
					ok = false;
					return 0.0;
				}
				double d = 0.0;
				std::memcpy(&d, data.data() + pos, 8);
				pos += 8;
				return d;
			}

			Proto ReadProto(int depth = 0) {
				Proto p;
				if (!ok || depth > 64) {
					ok = false;
					return p;
				}
				p.source = ReadString();
				p.linedef = ReadInt();
				p.lastlinedef = ReadInt();
				p.nups = ReadByte();
				p.numparams = ReadByte();
				p.is_vararg = ReadByte();
				p.maxstack = ReadByte();

				const int32_t num_code = ReadInt();
				if (!ok || num_code < 0 || num_code > 1000000) {
					ok = false;
					return p;
				}
				p.code.resize(static_cast<size_t>(num_code));
				for (int32_t i = 0; i < num_code; ++i) {
					p.code[static_cast<size_t>(i)] = static_cast<uint32_t>(ReadInt());
				}

				const int32_t num_k = ReadInt();
				if (!ok || num_k < 0 || num_k > 1000000) {
					ok = false;
					return p;
				}
				p.consts.resize(static_cast<size_t>(num_k));
				for (int32_t i = 0; i < num_k; ++i) {
					ConstVal c;
					c.type = ReadByte();
					if (c.type == 0) {
						// nil
					} else if (c.type == 1) {
						c.bval = ReadByte() != 0;
					} else if (c.type == 3) {
						c.nval = ReadNumber();
					} else if (c.type == 4) {
						c.sval = ReadString();
					} else {
						ok = false;
						return p;
					}
					p.consts[static_cast<size_t>(i)] = std::move(c);
				}

				const int32_t num_p = ReadInt();
				if (!ok || num_p < 0 || num_p > 100000) {
					ok = false;
					return p;
				}
				p.protos.resize(static_cast<size_t>(num_p));
				for (int32_t i = 0; i < num_p; ++i) {
					p.protos[static_cast<size_t>(i)] = ReadProto(depth + 1);
					if (!ok) {
						return p;
					}
				}

				const int32_t num_lineinfo = ReadInt();
				if (!ok || num_lineinfo < 0 || num_lineinfo > 1000000) {
					ok = false;
					return p;
				}
				for (int32_t i = 0; i < num_lineinfo; ++i) {
					(void)ReadInt();
				}

				const int32_t num_locvars = ReadInt();
				if (!ok || num_locvars < 0 || num_locvars > 100000) {
					ok = false;
					return p;
				}
				for (int32_t i = 0; i < num_locvars; ++i) {
					(void)ReadString();
					(void)ReadInt();
					(void)ReadInt();
				}

				const int32_t num_upvals = ReadInt();
				if (!ok || num_upvals < 0 || num_upvals > 100000) {
					ok = false;
					return p;
				}
				for (int32_t i = 0; i < num_upvals; ++i) {
					(void)ReadString();
				}
				return p;
			}
		};

		inline bool CompileProto(
			const Proto& p,
			const std::vector<std::string>& upval_names,
			int depth,
			bool is_top,
			std::string& out
		) {
			const std::string indent(static_cast<size_t>(depth) * 2, ' ');

			auto reg = [depth](uint32_t idx) -> std::string {
				return "R" + std::to_string(depth) + "_" + std::to_string(idx);
			};

			auto rk = [&](uint32_t val, bool& valid) -> std::string {
				if (val >= 256) {
					const size_t kidx = static_cast<size_t>(val - 256);
					if (kidx >= p.consts.size()) {
						valid = false;
						return "nil";
					}
					return FormatConst(p.consts[kidx]);
				}
				return reg(val);
			};

			auto global_ref = [&](uint32_t bx, bool& valid) -> std::string {
				if (bx >= p.consts.size()) {
					valid = false;
					return "_G[\"invalid\"]";
				}
				const auto& name = p.consts[bx].sval;
				if (IsIdentifier(name)) {
					return name;
				}
				return "_G[" + QuoteLuaString(name) + "]";
			};

			auto prop_get = [&](const std::string& table_expr, uint32_t c_val, bool& valid) -> std::string {
				if (c_val >= 256) {
					const size_t kidx = static_cast<size_t>(c_val - 256);
					if (kidx >= p.consts.size()) {
						valid = false;
						return table_expr + "[nil]";
					}
					const auto& c = p.consts[kidx];
					if (c.type == 4 && IsIdentifier(c.sval)) {
						return table_expr + "." + c.sval;
					}
				}
				return table_expr + "[" + rk(c_val, valid) + "]";
			};

			std::set<int32_t> jump_targets;
			const auto& code = p.code;
			const int32_t n = static_cast<int32_t>(code.size());
			for (int32_t pc = 0; pc < n; ++pc) {
				const uint32_t ins = code[static_cast<size_t>(pc)];
				const uint32_t op = ins & 0x3Fu;
				const uint32_t C = (ins >> 14) & 0x1FFu;
				const uint32_t Bx = (ins >> 14) & 0x3FFFFu;
				const int32_t sBx = static_cast<int32_t>(Bx) - 131071;

				if (op == 2 && C != 0) {
					jump_targets.insert(pc + 2);
				} else if (op == 22 || op == 31 || op == 32) {
					jump_targets.insert(pc + 1 + sBx);
				} else if (op == 23 || op == 24 || op == 25 || op == 26 || op == 27 || op == 33) {
					jump_targets.insert(pc + 2);
					if (pc + 1 < n) {
						const uint32_t next_ins = code[static_cast<size_t>(pc + 1)];
						const int32_t next_sBx = static_cast<int32_t>((next_ins >> 14) & 0x3FFFFu) - 131071;
						jump_targets.insert(pc + 2 + next_sBx);
					}
				} else if (op == 34 && C == 0) {
					pc += 1;
				} else if (op == 36) {
					if (Bx >= p.protos.size()) {
						return false;
					}
					pc += static_cast<int32_t>(p.protos[Bx].nups);
				}
			}

			std::ostringstream oss;
			const uint32_t start_reg = is_top ? 0u : static_cast<uint32_t>(p.numparams);
			const uint32_t end_reg = std::max<uint32_t>(p.maxstack, start_reg);
			oss << indent << "local ";
			for (uint32_t i = start_reg; i < end_reg; ++i) {
				oss << reg(i) << ", ";
			}
			oss << "_v" << depth << "\n";
			if (!is_top && (p.is_vararg & 4u) != 0) {
				oss << indent << reg(p.numparams) << " = {...}\n";
			}

			uint32_t top_base = 0;
			bool valid = true;
			for (int32_t pc = 0; pc < n; ++pc) {
				if (jump_targets.contains(pc)) {
					oss << indent << "::pc_" << pc << "::\n";
				}
				const uint32_t ins = code[static_cast<size_t>(pc)];
				const uint32_t op = ins & 0x3Fu;
				const uint32_t A = (ins >> 6) & 0xFFu;
				const uint32_t C = (ins >> 14) & 0x1FFu;
				const uint32_t B = (ins >> 23) & 0x1FFu;
				const uint32_t Bx = (ins >> 14) & 0x3FFFFu;
				const int32_t sBx = static_cast<int32_t>(Bx) - 131071;

				switch (op) {
					case 0: // MOVE
						oss << indent << reg(A) << " = " << reg(B) << "\n";
						break;
					case 1: // LOADK
						if (Bx >= p.consts.size()) return false;
						oss << indent << reg(A) << " = " << FormatConst(p.consts[Bx]) << "\n";
						break;
					case 2: // LOADBOOL
						oss << indent << reg(A) << " = " << (B != 0 ? "true" : "false") << "\n";
						if (C != 0) {
							oss << indent << "goto pc_" << (pc + 2) << "\n";
						}
						break;
					case 3: // LOADNIL
						oss << indent;
						for (uint32_t i = A; i <= B; ++i) {
							if (i > A) oss << ", ";
							oss << reg(i);
						}
						oss << " = nil\n";
						break;
					case 4: // GETUPVAL
						if (B >= upval_names.size()) return false;
						oss << indent << reg(A) << " = " << upval_names[B] << "\n";
						break;
					case 5: // GETGLOBAL
						oss << indent << reg(A) << " = " << global_ref(Bx, valid) << "\n";
						break;
					case 6: // GETTABLE
						oss << indent << reg(A) << " = " << prop_get(rk(B, valid), C, valid) << "\n";
						break;
					case 7: // SETGLOBAL
						oss << indent << global_ref(Bx, valid) << " = " << reg(A) << "\n";
						break;
					case 8: // SETUPVAL
						if (B >= upval_names.size()) return false;
						oss << indent << upval_names[B] << " = " << reg(A) << "\n";
						break;
					case 9: // SETTABLE
						oss << indent << prop_get(reg(A), B, valid) << " = " << rk(C, valid) << "\n";
						break;
					case 10: // NEWTABLE
						oss << indent << reg(A) << " = {}\n";
						break;
					case 11: // SELF
						oss << indent << reg(A + 1) << " = " << reg(B) << "; " << reg(A) << " = " << prop_get(reg(B), C, valid) << "\n";
						break;
					case 12: // ADD
						oss << indent << reg(A) << " = " << rk(B, valid) << " + " << rk(C, valid) << "\n";
						break;
					case 13: // SUB
						oss << indent << reg(A) << " = " << rk(B, valid) << " - " << rk(C, valid) << "\n";
						break;
					case 14: // MUL
						oss << indent << reg(A) << " = " << rk(B, valid) << " * " << rk(C, valid) << "\n";
						break;
					case 15: // DIV
						oss << indent << reg(A) << " = " << rk(B, valid) << " / " << rk(C, valid) << "\n";
						break;
					case 16: // MOD
						oss << indent << reg(A) << " = " << rk(B, valid) << " % " << rk(C, valid) << "\n";
						break;
					case 17: // POW
						oss << indent << reg(A) << " = " << rk(B, valid) << " ^ " << rk(C, valid) << "\n";
						break;
					case 18: // UNM
						oss << indent << reg(A) << " = -(" << rk(B, valid) << ")\n";
						break;
					case 19: // NOT
						oss << indent << reg(A) << " = not (" << rk(B, valid) << ")\n";
						break;
					case 20: // LEN
						oss << indent << reg(A) << " = #(" << rk(B, valid) << ")\n";
						break;
					case 21: // CONCAT
						oss << indent << reg(A) << " = ";
						for (uint32_t i = B; i <= C; ++i) {
							if (i > B) oss << " .. ";
							oss << reg(i);
						}
						oss << "\n";
						break;
					case 22: // JMP
						oss << indent << "goto pc_" << (pc + 1 + sBx) << "\n";
						break;
					case 23: // EQ
					case 24: // LT
					case 25: { // LE
						if (pc + 1 >= n) return false;
						const uint32_t next_ins = code[static_cast<size_t>(pc + 1)];
						const int32_t next_sBx = static_cast<int32_t>((next_ins >> 14) & 0x3FFFFu) - 131071;
						const int32_t jmp_target = pc + 2 + next_sBx;
						std::string cond;
						if (op == 23) {
							cond = "(" + rk(B, valid) + (A != 0 ? " == " : " ~= ") + rk(C, valid) + ")";
						} else if (op == 24) {
							cond = (A != 0 ? "(" : "not (") + rk(B, valid) + " < " + rk(C, valid) + ")";
						} else {
							cond = (A != 0 ? "(" : "not (") + rk(B, valid) + " <= " + rk(C, valid) + ")";
						}
						oss << indent << "if " << cond << " then goto pc_" << jmp_target << " else goto pc_" << (pc + 2) << " end\n";
						break;
					}
					case 26: { // TEST
						if (pc + 1 >= n) return false;
						const uint32_t next_ins = code[static_cast<size_t>(pc + 1)];
						const int32_t next_sBx = static_cast<int32_t>((next_ins >> 14) & 0x3FFFFu) - 131071;
						const int32_t jmp_target = pc + 2 + next_sBx;
						const std::string cond = (C != 0 ? "" : "not ") + reg(A);
						oss << indent << "if " << cond << " then goto pc_" << jmp_target << " else goto pc_" << (pc + 2) << " end\n";
						break;
					}
					case 27: { // TESTSET
						if (pc + 1 >= n) return false;
						const uint32_t next_ins = code[static_cast<size_t>(pc + 1)];
						const int32_t next_sBx = static_cast<int32_t>((next_ins >> 14) & 0x3FFFFu) - 131071;
						const int32_t jmp_target = pc + 2 + next_sBx;
						const std::string cond = (C != 0 ? "" : "not ") + reg(B);
						oss << indent << "if " << cond << " then " << reg(A) << " = " << reg(B) << "; goto pc_" << jmp_target << " else goto pc_" << (pc + 2) << " end\n";
						break;
					}
					case 28: { // CALL
						std::ostringstream args;
						if (B == 0) {
							for (uint32_t i = A + 1; i < top_base; ++i) {
								args << reg(i) << ", ";
							}
							args << "unpack(_v" << depth << ")";
						} else {
							for (uint32_t i = A + 1; i < A + B; ++i) {
								if (i > A + 1) args << ", ";
								args << reg(i);
							}
						}
						if (C == 0) {
							oss << indent << "_v" << depth << " = {" << reg(A) << "(" << args.str() << ")}\n";
							top_base = A;
						} else if (C == 1) {
							oss << indent << reg(A) << "(" << args.str() << ")\n";
						} else {
							oss << indent;
							for (uint32_t i = A; i < A + C - 1; ++i) {
								if (i > A) oss << ", ";
								oss << reg(i);
							}
							oss << " = " << reg(A) << "(" << args.str() << ")\n";
						}
						break;
					}
					case 29: { // TAILCALL
						std::ostringstream args;
						if (B == 0) {
							for (uint32_t i = A + 1; i < top_base; ++i) {
								args << reg(i) << ", ";
							}
							args << "unpack(_v" << depth << ")";
						} else {
							for (uint32_t i = A + 1; i < A + B; ++i) {
								if (i > A + 1) args << ", ";
								args << reg(i);
							}
						}
						oss << indent << "do return " << reg(A) << "(" << args.str() << ") end\n";
						break;
					}
					case 30: { // RETURN
						if (B == 0) {
							oss << indent << "do return ";
							for (uint32_t i = A; i < top_base; ++i) {
								oss << reg(i) << ", ";
							}
							oss << "unpack(_v" << depth << ") end\n";
						} else if (B == 1) {
							oss << indent << "do return end\n";
						} else {
							oss << indent << "do return ";
							for (uint32_t i = A; i < A + B - 1; ++i) {
								if (i > A) oss << ", ";
								oss << reg(i);
							}
							oss << " end\n";
						}
						break;
					}
					case 31: { // FORLOOP
						const int32_t target = pc + 1 + sBx;
						oss << indent << reg(A) << " = " << reg(A) << " + " << reg(A + 2) << "\n";
						oss << indent << "if (" << reg(A + 2) << " > 0 and " << reg(A) << " <= " << reg(A + 1)
						    << ") or (" << reg(A + 2) << " <= 0 and " << reg(A) << " >= " << reg(A + 1)
						    << ") then " << reg(A + 3) << " = " << reg(A) << "; goto pc_" << target << " end\n";
						break;
					}
					case 32: { // FORPREP
						const int32_t target = pc + 1 + sBx;
						oss << indent << reg(A) << " = " << reg(A) << " - " << reg(A + 2) << "; goto pc_" << target << "\n";
						break;
					}
					case 33: { // TFORLOOP
						if (pc + 1 >= n) return false;
						const uint32_t next_ins = code[static_cast<size_t>(pc + 1)];
						const int32_t next_sBx = static_cast<int32_t>((next_ins >> 14) & 0x3FFFFu) - 131071;
						const int32_t jmp_target = pc + 2 + next_sBx;
						oss << indent;
						for (uint32_t i = A + 3; i < A + 3 + C; ++i) {
							if (i > A + 3) oss << ", ";
							oss << reg(i);
						}
						oss << " = " << reg(A) << "(" << reg(A + 1) << ", " << reg(A + 2) << ")\n";
						oss << indent << "if " << reg(A + 3) << " ~= nil then " << reg(A + 2) << " = " << reg(A + 3)
						    << "; goto pc_" << jmp_target << " else goto pc_" << (pc + 2) << " end\n";
						break;
					}
					case 34: { // SETLIST
						uint32_t c_block = C;
						if (c_block == 0) {
							pc += 1;
							if (pc >= n) return false;
							c_block = code[static_cast<size_t>(pc)];
						}
						const uint32_t base_idx = (c_block - 1) * 50;
						if (B > 0) {
							oss << indent;
							for (uint32_t i = 1; i <= B; ++i) {
								if (i > 1) oss << "; ";
								oss << reg(A) << "[" << (base_idx + i) << "] = " << reg(A + i);
							}
							oss << "\n";
						} else {
							const uint32_t fixed_cnt = top_base > A ? (top_base - A - 1) : 0;
							for (uint32_t i = 1; i <= fixed_cnt; ++i) {
								oss << indent << reg(A) << "[" << (base_idx + i) << "] = " << reg(A + i) << "\n";
							}
							oss << indent << "for _i = 1, #_v" << depth << " do " << reg(A)
							    << "[" << (base_idx + fixed_cnt) << " + _i] = _v" << depth << "[_i] end\n";
						}
						break;
					}
					case 35: // CLOSE
						break;
					case 36: { // CLOSURE
						if (Bx >= p.protos.size()) return false;
						const auto& child = p.protos[Bx];
						std::vector<std::string> child_upvals;
						child_upvals.reserve(child.nups);
						for (uint8_t u_idx = 0; u_idx < child.nups; ++u_idx) {
							pc += 1;
							if (pc >= n) return false;
							const uint32_t u_ins = code[static_cast<size_t>(pc)];
							const uint32_t u_op = u_ins & 0x3Fu;
							const uint32_t u_B = (u_ins >> 23) & 0x1FFu;
							if (u_op == 0) {
								child_upvals.push_back(reg(u_B));
							} else if (u_op == 4) {
								if (u_B >= upval_names.size()) return false;
								child_upvals.push_back(upval_names[u_B]);
							} else {
								return false;
							}
						}
						oss << indent << reg(A) << " = function(";
						for (uint8_t i = 0; i < child.numparams; ++i) {
							if (i > 0) oss << ", ";
							oss << "R" << (depth + 1) << "_" << static_cast<uint32_t>(i);
						}
						if ((child.is_vararg & 3u) != 0) {
							if (child.numparams > 0) oss << ", ";
							oss << "...";
						}
						oss << ")\n";
						std::string body;
						if (!CompileProto(child, child_upvals, depth + 1, false, body)) {
							return false;
						}
						oss << body;
						oss << indent << "end\n";
						break;
					}
					case 37: { // VARARG
						if (B == 0) {
							oss << indent << "_v" << depth << " = {...}\n";
							top_base = A;
						} else {
							oss << indent;
							for (uint32_t i = A; i < A + B - 1; ++i) {
								if (i > A) oss << ", ";
								oss << reg(i);
							}
							oss << " = ...\n";
						}
						break;
					}
					default:
						return false;
				}
				if (!valid) {
					return false;
				}
			}

			out = oss.str();
			return true;
		}

	} // namespace Detail

	inline std::optional<std::string> TranspileBytecode(std::string_view data) {
		if (!IsLua51Bytecode(data)) {
			return std::nullopt;
		}
		Detail::Reader reader;
		reader.data = data;
		reader.pos = 12;
		reader.size_t_sz = static_cast<uint8_t>(data[8]);
		reader.num_sz = static_cast<uint8_t>(data[10]);

		const Proto top = reader.ReadProto(0);
		if (!reader.ok) {
			return std::nullopt;
		}
		std::string lua_source;
		if (!Detail::CompileProto(top, {}, 0, true, lua_source)) {
			return std::nullopt;
		}
		return lua_source;
	}

	inline bool TranspileFile(const std::filesystem::path& inputPath, const std::filesystem::path& outputPath) {
		std::ifstream in(inputPath, std::ios::binary);
		if (!in.is_open()) {
			return false;
		}
		std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		in.close();

		std::filesystem::create_directories(outputPath.parent_path());
		if (IsLua51Bytecode(data)) {
			auto transpiled = TranspileBytecode(data);
			if (!transpiled.has_value()) {
				return false;
			}
			std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) {
				return false;
			}
			out.write(transpiled->data(), static_cast<std::streamsize>(transpiled->size()));
			return out.good();
		}

		// Plain-text Lua file: write directly
		std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
		if (!out.is_open()) {
			return false;
		}
		out.write(data.data(), static_cast<std::streamsize>(data.size()));
		return out.good();
	}

	inline std::string_view GetCleanCitadelBossLaunchLua() {
		static constexpr std::string_view kScript = R"lua(require("Abilities!ability_citadel_boss_missile.lua")
require("Modifiers!modifier_citadel_boss_passive.lua")
require("Lua!TargetUtils.lua")
nAbility_Citadel_Boss_Launch = Class.newClass()
nAbility_Citadel_Boss_Launch.namespace = "nAbility_Citadel_Boss_Launch"
nAbility_Citadel_Boss_Launch.requiresAgent = true
nAbility_Citadel_Boss_Launch.cooldown = {
  30,
  30,
  30
}
nAbility_Citadel_Boss_Launch.manaCost = {
  0,
  0,
  0
}
nAbility_Citadel_Boss_Launch.descriptors = nDescriptors.IsPhysicalDamage
nAbility_Citadel_Boss_Launch.launchAnim = nAbility.PreloadAnimation("ctd_boss_tc_attack1_launch", "CitadelBossLaunch")
nAbility_Citadel_Boss_Launch.landAnim = nAbility.PreloadAnimation("ctd_boss_tc_attack1_land", "CitadelBossLaunch")
nAbility_Citadel_Boss_Launch.timeToInvulnerable = 1
nAbility_Citadel_Boss_Launch.timeToInvisble = 2
nAbility_Citadel_Boss_Launch.launchAnimTime = 1.433333
nAbility_Citadel_Boss_Launch.landAnimTime = 1.083333
nAbility_Citadel_Boss_Launch.landHitTime = 0.2
nAbility_Citadel_Boss_Launch.missileDrops = 12
nAbility_Citadel_Boss_Launch.timeBetweenDrops = 0.5
nAbility_Citadel_Boss_Launch.dropAreaRadius = 12
nAbility_Citadel_Boss_Launch.pauseAfterLastDrop = 2
nAbility_Citadel_Boss_Launch.knockbackModifier = nUtil.ToGUID("0x0139de24")
nAbility_Citadel_Boss_Launch.knockbackRadius = 1
nAbility_Citadel_Boss_Launch.damage = {
  {20, 30},
  {30, 40},
  {40, 50}
}
nAbility_Citadel_Boss_Launch.damageType = nDamageTypes.Technology
nAbility_Citadel_Boss_Launch.damageSource = nDamageSources.Physical
nAbility_Citadel_Boss_Launch.damageCoefficient = 0
nAbility.RegisterAbility("CitadelBossLaunch", nAbility_Citadel_Boss_Launch)
nAbility_Citadel_Boss_Launch[nAbilityFns.Tick] = function()
  local self = nAbility_Citadel_Boss_Launch
  local agentId = nAbility.GetAgentID()
  local attrSnap = nAbility.GetAgentAttributeSnapshot()
  nGameObject.SetAnimationState(agentId, self.launchAnim)
  nThread.WaitForXSeconds(self.timeToInvulnerable)
  local debuffs = nModifier.GetModifiersMatchingDescriptor(agentId, nDescriptors.IsDebuff)
  for _, mod in ipairs(debuffs) do
    nModifier.MarkForDelete(mod)
  end
  local passiveMod = nModifier.GetFirstModifierByGUID(agentId, nUtil.SPID("CitadelBossPassive"))
  nAttribute.AddAttributeModifier(agentId, nAttributeType.Intangible, 1)
  nPhysics.RemovePhysicsForObject(agentId)
  nGameObject.SetNavCollision(agentId, false)
  local turret = kObjIDNone
  if passiveMod ~= kObjIDNone then
    turret = nModifier.CallFunctionInContext(passiveMod, nModifier_Citadel_Boss_Passive.GetTurret)
  end
  if turret ~= kObjIDNone then
    nPhysics.AddPhysicsForObject(turret)
  end
  local drops = 0
  local posX, posY, posZ = nGameObject.GetPosition(agentId)
  while drops < self.missileDrops do
    nThread.WaitForXSeconds(self.timeBetweenDrops)
    local dropX, dropY, dropZ = nil, nil, nil
    local players = nPlayer.GetPlayerControlledObjects()
    local target = players[math.random(5)]
    if target == nil or not nGameObject.IsAlive(target) then
      local angle = math.random(0, math.pi * 2)
      local dist = math.random(0, self.dropAreaRadius)
      dropX = posX + math.cos(angle) * dist
      dropY = posY + math.sin(angle) * dist
      dropZ = nLocomotion.GetTerrainHeight(dropX, dropY, posZ)
    else
      dropX, dropY, dropZ = nGameObject.GetPosition(target)
    end
    nAbility_Citadel_Boss_Missile.DropMissile(agentId, attrSnap, dropX, dropY, dropZ)
    drops = drops + 1
  end
  nThread.WaitForXSeconds(self.pauseAfterLastDrop)
  if passiveMod ~= kObjIDNone then
    turret = nModifier.CallFunctionInContext(passiveMod, nModifier_Citadel_Boss_Passive.TurretCheck)
    if turret ~= kObjIDNone then
      nPhysics.RemovePhysicsForObject(turret)
    end
    coroutine.yield()
    nModifier.CallFunctionInContext(passiveMod, nModifier_Citadel_Boss_Passive.TeleportNearArenaCenter)
    coroutine.yield()
  end
  nGameObject.SetAnimationState(agentId, self.landAnim)
  nThread.WaitForXSeconds(self.landHitTime)
  local targets = nTargetUtils.GetTargetsAroundAttacker(agentId, self.knockbackRadius + nGameObject.GetFootprintRadius(agentId))
  for _, target in ipairs(targets) do
    if nGameObject.TakeDamage(attrSnap, target, nAbility.GetRankedValue(self.damage), self.damageType, self.damageSource, self.damageCoefficient, self.descriptors) then
      nModifier.RequestModifier(target, agentId, self.knockbackModifier)
    end
  end
  nPhysics.AddPhysicsForObject(agentId)
  nGameObject.SetNavCollision(agentId, true)
  nThread.WaitForXSeconds(self.landAnimTime - self.landHitTime)
end
)lua";
		return kScript;
	}

	inline std::string_view GetCleanTemplateAbilityConeLua() {
		static constexpr std::string_view kScript = R"lua(require("Lua!Vector.lua")
nAbility_Cone_Template = Class.newClass()
nAbility_Cone_Template.hasTick = true
nAbility_Cone_Template.hasDeactivate = true
nAbility_Cone_Template.requiresAgent = true
nAbility_Cone_Template.descriptors = nDescriptors.IsEnergyDamage
nAbility_Cone_Template.primaryAttributeStat = nAbilityPrimaryStat.Damage
nAbility_Cone_Template.cooldownType = nCooldownType.Manual
nAbility_Cone_Template.animationstate = kInvalidGUID
nAbility_Cone_Template.timetohit = 0
nAbility_Cone_Template.duration = 0
nAbility_Cone_Template.fxEvent = kInvalidGUID
nAbility_Cone_Template.hitEvent = kInvalidGUID
nAbility_Cone_Template.modifiers = {}
nAbility_Cone_Template.damageCreatureCount = -1
nAbility_Cone_Template.damageRepeatInterval = {
  0,
  0,
  0,
  0,
  0
}
nAbility_Cone_Template.damagedObjectTypes = nSporeLabs.damageableObjectTypes
function nAbility_Cone_Template.TimeToHitAction(self)
end
function nAbility_Cone_Template.MakeHitArc(self)
  local agentId = nAbility.GetAgentID()
  local radius = GetRankedValueHelper(self.radius, nAbility.GetRank())
  return nTargetUtils.MakeArc(agentId, radius, self.angle, true)
end
function nAbility_Cone_Template.DoDamage(self)
  local agentId = nAbility.GetAgentID()
  if nObjectManager.IsValidObject(agentId) then
    local team = nGameObject.GetTeam(agentId)
    local attrSnap = nAbility.GetAgentAttributeSnapshot()
    local posX, posY, posZ = nGameObject.GetPosition(agentId)
    local faceX, faceY, faceZ = nGameObject.GetFacing(agentId)
    faceX, faceY, faceZ = nVector.normalize(faceX, faceY, faceZ)
    local hitCount = 0
    local radius = nAbility.GetRankedValue(self.radius)
    if nBit.And(self.descriptors, nDescriptors.IsAoE) == nDescriptors.IsAoE then
      radius = radius * (1 + nAttribute.GetAttributeValue(agentId, nAttributeType.AoERadius))
    end
    local objs = nObjectManager.GetObjectsInRadius(posX, posY, posZ, nAbility.GetRankedValue(self.radius), self.damagedObjectTypes)
    local arc = self:MakeHitArc()
    nAbility.DebugDrawDamageArc(nAbility.GetAbilityInstanceID(), arc)
    for _, obj in pairs(objs) do
      if obj ~= agentId and nGameObject.ValidateHostileTarget(team, obj) ~= false and (self.damageCreatureCount == -1 or hitCount < self.damageCreatureCount) then
        local objX, objY, objZ = nGameObject.GetPosition(obj)
        objX = objX - posX
        objY = objY - posY
        objZ = objZ - posZ
        objX, objY, objZ = nVector.normalize(objX, objY, objZ)
        local dot = nVector.dot(objX, objY, objZ, faceX, faceY, faceZ)
        local inCone = false
        if (dot > 0 and self.angle < 180) or self.angle >= 180 then
          if math.rad(self.angle * 0.5) > math.acos(dot) then
            inCone = true
          end
        end
        if inCone then
          local dmg = nAbility.GetRankedValue(self.damage)
          local hit = true
          if dmg[2] ~= 0 then
            hit = nGameObject.TakeDamage(attrSnap, obj, nAbility.GetRankedValue(self.damage), self.damageType, self.damageSource, self.damageCoefficient, self.descriptors)
          end
          if hit then
            if self.hitEffect ~= kInvalidGUID then
              local ev = {
                asset = self.hitEffect,
                objectId = obj
              }
              nEvent.Notify(ev)
            end
            for _, mod in ipairs(self.modifiers) do
              nModifier.RequestModifier(obj, agentId, mod, attrSnap, nAbility.GetRank())
            end
          end
          hitCount = hitCount + 1
        end
      end
    end
  end
end
function nAbility_Cone_Template.tick(self)
  local agentId = nAbility.GetAgentID()
  local attrSnap = nAbility.GetAgentAttributeSnapshot()
  local duration = nAbility.GetRankedValue(self.duration)
  if nBit.And(self.descriptors, nDescriptors.IsAoE) == nDescriptors.IsAoE then
    duration = duration * (1 + nAttribute.GetAttributeValue(agentId, nAttributeType.AoEDurationIncrease))
  end
  if self.animationstate ~= kInvalidGUID then
    nGameObject.SetAnimationState(agentId, self.animationstate)
    nThread.WaitUntilTime(self.timetohit, attrSnap)
    nAbility.PayCooldownAndMana()
    duration = duration - self.timetohit
    self:TimeToHitAction()
  end
  if self.fxEvent ~= kInvalidGUID then
    local posX, posY, posZ = nGameObject.GetPosition(agentId)
    local faceX, faceY, faceZ = nGameObject.GetFacing(agentId)
    faceX, faceY, faceZ = nVector.normalize(faceX, faceY, faceZ)
    local ev = {
      asset = self.fxEvent,
      position = {
        posX,
        posY,
        posZ
      },
      facing = {
        faceX,
        faceY,
        faceZ
      }
    }
    nEvent.Notify(ev)
  end
  local repeatInterval = nAbility.GetRankedValue(self.damageRepeatInterval)
  repeat
    self:DoDamage()
    nThread.WaitForXSeconds(repeatInterval, attrSnap)
    duration = duration - repeatInterval
  until not (duration > repeatInterval and repeatInterval > 0 and nGameObject.IsAlive(agentId))
  if nGameObject.IsAlive(agentId) then
    nThread.WaitUntilTime(duration, attrSnap)
  end
end
function nAbility_Cone_Template.deactivate(self)
end
)lua";
		return kScript;
	}

	inline std::optional<std::string> GetKnownBrokenScriptRepair(std::string_view stem, std::string_view content) {
		if (IsLua51Bytecode(content)) {
			return TranspileBytecode(content);
		}
		if (stem == "ability_citadel_boss_launch") {
			// Detect unluac syntax error or ad-hoc broken decompilation with unassigned L4_4/L5_5
			if (content.find("for L7_7, L8_8 in L4_4(L5_5) do") != std::string_view::npos ||
			    content.find("if L4_3 ~= L13_3 then") != std::string_view::npos) {
				return std::string(GetCleanCitadelBossLaunchLua());
			}
		}
		if (stem == "template_ability_cone") {
			// Detect unluac syntax error or ad-hoc broken decompilation with infinite repeat loop
			if (content.find("until not L5_41") != std::string_view::npos ||
			    content.find("for L17_21, L18_22 in L14_18(L15_19) do") != std::string_view::npos) {
				return std::string(GetCleanTemplateAbilityConeLua());
			}
		}
		return std::nullopt;
	}

	inline std::optional<uint32_t> ParseHexStemId(std::string_view stem) {
		if (stem.size() < 3 || stem.size() > 10) {
			return std::nullopt;
		}
		if (stem[0] != '0' || (stem[1] != 'x' && stem[1] != 'X')) {
			return std::nullopt;
		}
		uint32_t value = 0;
		for (size_t i = 2; i < stem.size(); ++i) {
			const unsigned char c = static_cast<unsigned char>(stem[i]);
			if (!std::isxdigit(c)) {
				return std::nullopt;
			}
			value <<= 4;
			if (c >= '0' && c <= '9') {
				value |= static_cast<uint32_t>(c - '0');
			} else if (c >= 'a' && c <= 'f') {
				value |= static_cast<uint32_t>(c - 'a' + 10);
			} else {
				value |= static_cast<uint32_t>(c - 'A' + 10);
			}
		}
		return value;
	}

	inline std::vector<std::string> ExtractRegisteredAbilityNames(std::string_view luaSource) {
		std::vector<std::string> names;
		auto addName = [&](std::string_view candidate) {
			if (candidate.empty()) {
				return;
			}
			for (const auto& existing : names) {
				if (existing == candidate) {
					return;
				}
			}
			names.emplace_back(candidate);
		};

		// 1. Match .abilityName = "Name" (or L1_1 = "Name" on the preceding line)
		size_t pos = 0;
		while ((pos = luaSource.find(".abilityName", pos)) != std::string_view::npos) {
			const size_t propStart = pos;
			pos += 12;
			while (pos < luaSource.size() && (luaSource[pos] == ' ' || luaSource[pos] == '\t')) {
				++pos;
			}
			if (pos < luaSource.size() && luaSource[pos] == '=') {
				++pos;
				while (pos < luaSource.size() && (luaSource[pos] == ' ' || luaSource[pos] == '\t')) {
					++pos;
				}
				if (pos < luaSource.size() && luaSource[pos] == '"') {
					const size_t start = pos + 1;
					const size_t end = luaSource.find('"', start);
					if (end != std::string_view::npos) {
						addName(luaSource.substr(start, end - start));
						pos = end + 1;
					}
				} else if (propStart > 0) {
					const size_t curLineStart = luaSource.rfind('\n', propStart);
					if (curLineStart != std::string_view::npos && curLineStart > 0) {
						const size_t prevLineStart = luaSource.rfind('\n', curLineStart - 1);
						const size_t startIdx = (prevLineStart == std::string_view::npos) ? 0 : (prevLineStart + 1);
						const std::string_view prevLine = luaSource.substr(startIdx, curLineStart - startIdx);
						const size_t qStart = prevLine.find('"');
						if (qStart != std::string_view::npos) {
							const size_t qEnd = prevLine.find('"', qStart + 1);
							if (qEnd != std::string_view::npos) {
								addName(prevLine.substr(qStart + 1, qEnd - qStart - 1));
							}
						}
					}
				}
			}
		}

		// 2. Match RegisterAbility("Name" or RegisterAbility followed by R/L assignment = "Name"
		pos = 0;
		while ((pos = luaSource.find("RegisterAbility", pos)) != std::string_view::npos) {
			pos += 15;
			size_t scan = pos;
			while (scan < luaSource.size() && (luaSource[scan] == ' ' || luaSource[scan] == '\t')) {
				++scan;
			}
			if (scan < luaSource.size() && luaSource[scan] == '(') {
				++scan;
				while (scan < luaSource.size() && std::isspace(static_cast<unsigned char>(luaSource[scan]))) {
					++scan;
				}
				if (scan < luaSource.size() && luaSource[scan] == '"') {
					const size_t start = scan + 1;
					const size_t end = luaSource.find('"', start);
					if (end != std::string_view::npos) {
						addName(luaSource.substr(start, end - start));
					}
				}
			} else if (scan < luaSource.size() && (luaSource[scan] == '\r' || luaSource[scan] == '\n')) {
				while (scan < luaSource.size() && std::isspace(static_cast<unsigned char>(luaSource[scan]))) {
					++scan;
				}
				const size_t lineEnd = luaSource.find('\n', scan);
				const std::string_view nextLine = luaSource.substr(
					scan,
					lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - scan
				);
				const size_t eqPos = nextLine.find('=');
				if (eqPos != std::string_view::npos) {
					const size_t qStart = nextLine.find('"', eqPos + 1);
					if (qStart != std::string_view::npos) {
						const size_t qEnd = nextLine.find('"', qStart + 1);
						if (qEnd != std::string_view::npos) {
							addName(nextLine.substr(qStart + 1, qEnd - qStart - 1));
						}
					}
				}
			}
		}

		return names;
	}

} // namespace Game::Lua51Transpiler

#endif
