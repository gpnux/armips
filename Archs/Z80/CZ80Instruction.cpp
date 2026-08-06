#include "Archs/Z80/CZ80Instruction.h"

#include "Core/Common.h"
#include "Core/FileManager.h"
#include "Core/Misc.h"
#include "Core/SymbolData.h"

#include <array>
#include <map>
#include <optional>

namespace
{
using Bytes = std::vector<uint8_t>;

const std::map<std::string, int> r8 = {
	{ "b", 0 }, { "c", 1 }, { "d", 2 }, { "e", 3 },
	{ "h", 4 }, { "l", 5 }, { "hl", 6 }, { "a", 7 },
};
const std::map<std::string, int> r16 = {
	{ "bc", 0 }, { "de", 1 }, { "hl", 2 }, { "sp", 3 },
};
const std::map<std::string, int> stack = {
	{ "bc", 0 }, { "de", 1 }, { "hl", 2 }, { "af", 3 },
};
const std::map<std::string, int> conditions = {
	{ "nz", 0 }, { "z", 1 }, { "nc", 2 }, { "c", 3 },
	{ "po", 4 }, { "pe", 5 }, { "p", 6 }, { "m", 7 },
};
const std::map<std::string, int> shifts = {
	{ "rlc", 0 }, { "rrc", 1 }, { "rl", 2 }, { "rr", 3 },
	{ "sla", 4 }, { "sra", 5 }, { "sll", 6 }, { "srl", 7 },
};

class Encoder
{
public:
	Encoder(std::string mnemonic, std::vector<Z80Operand>& operands, Z80Mode mode, int64_t pc)
		: mnemonic(std::move(mnemonic)), operands(operands), mode(mode), pc(pc) { }

	Bytes run()
	{
		Bytes result;
		if (encodeFixed(result) || encodeLoad(result) || encodeIncDec(result) ||
			encodeStack(result) || encodeAlu(result) || encodeBits(result) ||
			encodeControl(result) || encodeIo(result))
			return result;
		fail("unsupported instruction or operand combination");
		return {};
	}

	bool needsRevalidation() const { return unresolved; }
	const std::string& getError() const { return error; }

private:
	bool fail(const std::string& message)
	{
		if (error.empty())
			error = mnemonic + ": " + message;
		return true;
	}

	bool count(size_t value) const { return operands.size() == value; }
	bool name(size_t index, const char* value) const
	{
		return index < operands.size() && operands[index].kind == Z80OperandKind::Name &&
			operands[index].name == value;
	}
	bool indirectName(size_t index, const char* value) const
	{
		return index < operands.size() && operands[index].kind == Z80OperandKind::IndirectName &&
			operands[index].name == value;
	}
	bool immediate(size_t index) const
	{
		return index < operands.size() && operands[index].kind == Z80OperandKind::Immediate;
	}
	bool indirectImmediate(size_t index) const
	{
		return index < operands.size() && operands[index].kind == Z80OperandKind::IndirectImmediate;
	}

	std::optional<int> reg8(size_t index) const
	{
		if (index >= operands.size())
			return std::nullopt;
		const auto& operand = operands[index];
		if (operand.kind == Z80OperandKind::Name)
		{
			auto it = r8.find(operand.name);
			if (it != r8.end() && operand.name != "hl")
				return it->second;
		}
		if (operand.kind == Z80OperandKind::IndirectName && operand.name == "hl")
			return 6;
		return std::nullopt;
	}

	std::optional<int> reg16(size_t index) const
	{
		if (index >= operands.size() || operands[index].kind != Z80OperandKind::Name)
			return std::nullopt;
		auto it = r16.find(operands[index].name);
		return it == r16.end() ? std::nullopt : std::optional<int>(it->second);
	}

	std::optional<int> condition(size_t index) const
	{
		if (index >= operands.size() || operands[index].kind != Z80OperandKind::Name)
			return std::nullopt;
		auto it = conditions.find(operands[index].name);
		if (it == conditions.end() || (mode == Z80Mode::Sm83 && it->second > 3))
			return std::nullopt;
		return it->second;
	}

	std::optional<uint8_t> indexPrefix(size_t index) const
	{
		if (index >= operands.size() || operands[index].kind != Z80OperandKind::Name)
			return std::nullopt;
		if (operands[index].name == "ix") return 0xDD;
		if (operands[index].name == "iy") return 0xFD;
		return std::nullopt;
	}

	int64_t value(size_t index, bool& known)
	{
		known = false;
		if (index >= operands.size() || !operands[index].expression.isLoaded())
		{
			fail("expected expression");
			return 0;
		}
		int64_t result = 0;
		known = operands[index].expression.evaluateInteger(result);
		if (!known)
			unresolved = true;
		return result;
	}

	int64_t value(size_t index)
	{
		bool known;
		return value(index, known);
	}

	uint8_t byte(size_t index)
	{
		bool known;
		int64_t result = value(index, known);
		if (known && (result < -128 || result > 0xFF))
			fail("8-bit immediate out of range");
		return uint8_t(result);
	}

	uint8_t displacement(size_t index)
	{
		bool known;
		int64_t result = value(index, known);
		if (known && (result < -128 || result > 127))
			fail("indexed displacement out of range");
		return uint8_t(result);
	}

	void appendWord(Bytes& result, int64_t number)
	{
		if (number < -0x8000 || number > 0xFFFF)
			fail("16-bit immediate out of range");
		result.push_back(uint8_t(number));
		result.push_back(uint8_t(number >> 8));
	}

	void opcodeWord(Bytes& result, uint8_t opcode, size_t operand)
	{
		result.push_back(opcode);
		appendWord(result, value(operand));
	}

	uint8_t relative(size_t index)
	{
		bool known;
		int64_t target = value(index, known);
		int64_t offset = target - (pc + 2);
		if (known && (offset < -128 || offset > 127))
			fail("relative branch target out of range");
		return uint8_t(offset);
	}

	bool encodeFixed(Bytes& out)
	{
		if (!count(0))
			return false;
		static const std::map<std::string, Bytes> standard = {
			{ "nop", { 0x00 } }, { "di", { 0xF3 } }, { "ei", { 0xFB } },
			{ "halt", { 0x76 } }, { "ret", { 0xC9 } }, { "reti", { 0xED, 0x4D } },
			{ "retn", { 0xED, 0x45 } }, { "exx", { 0xD9 } }, { "cpl", { 0x2F } },
			{ "scf", { 0x37 } }, { "ccf", { 0x3F } }, { "daa", { 0x27 } },
			{ "rlca", { 0x07 } }, { "rrca", { 0x0F } }, { "rla", { 0x17 } },
			{ "rra", { 0x1F } }, { "ldir", { 0xED, 0xB0 } },
		};
		static const std::map<std::string, Bytes> sm83 = {
			{ "nop", { 0x00 } }, { "stop", { 0x10, 0x00 } }, { "di", { 0xF3 } },
			{ "ei", { 0xFB } }, { "halt", { 0x76 } }, { "ret", { 0xC9 } },
			{ "reti", { 0xD9 } }, { "cpl", { 0x2F } }, { "scf", { 0x37 } },
			{ "ccf", { 0x3F } }, { "daa", { 0x27 } }, { "rlca", { 0x07 } },
			{ "rrca", { 0x0F } }, { "rla", { 0x17 } }, { "rra", { 0x1F } },
		};
		const auto& table = mode == Z80Mode::Sm83 ? sm83 : standard;
		auto it = table.find(mnemonic);
		if (it == table.end())
			return false;
		out = it->second;
		return true;
	}

	bool encodeLoad(Bytes& out)
	{
		if (mnemonic == "ldh")
		{
			if (mode != Z80Mode::Sm83 || !count(2))
				return false;
			if (indirectImmediate(0) && name(1, "a"))
				out = { 0xE0, byte(0) };
			else if (name(0, "a") && indirectImmediate(1))
				out = { 0xF0, byte(1) };
			else
				return false;
			return true;
		}
		if (mnemonic != "ld" || !count(2))
			return false;

		if (mode == Z80Mode::Sm83)
		{
			if (indirectName(0, "hl+") && name(1, "a")) { out = { 0x22 }; return true; }
			if (indirectName(0, "hl-") && name(1, "a")) { out = { 0x32 }; return true; }
			if (name(0, "a") && indirectName(1, "hl+")) { out = { 0x2A }; return true; }
			if (name(0, "a") && indirectName(1, "hl-")) { out = { 0x3A }; return true; }
			if (indirectName(0, "c") && name(1, "a")) { out = { 0xE2 }; return true; }
			if (name(0, "a") && indirectName(1, "c")) { out = { 0xF2 }; return true; }
			if (indirectImmediate(0) && name(1, "sp"))
			{
				out = { 0x08 }; appendWord(out, value(0)); return true;
			}
			if (name(0, "sp") && name(1, "hl")) { out = { 0xF9 }; return true; }
		}
		else
		{
			if (name(0, "a") && name(1, "i")) { out = { 0xED, 0x57 }; return true; }
			if (name(0, "a") && name(1, "r")) { out = { 0xED, 0x5F }; return true; }
			if (name(0, "i") && name(1, "a")) { out = { 0xED, 0x47 }; return true; }
			if (name(0, "r") && name(1, "a")) { out = { 0xED, 0x4F }; return true; }
			if (name(0, "sp") && name(1, "hl")) { out = { 0xF9 }; return true; }

			if (auto prefix = indexPrefix(0))
			{
				if (immediate(1)) { out = { *prefix, 0x21 }; appendWord(out, value(1)); return true; }
				if (indirectImmediate(1)) { out = { *prefix, 0x2A }; appendWord(out, value(1)); return true; }
			}
			if (auto prefix = indexPrefix(1))
			{
				if (name(0, "sp")) { out = { *prefix, 0xF9 }; return true; }
				if (indirectImmediate(0)) { out = { *prefix, 0x22 }; appendWord(out, value(0)); return true; }
			}
			if (operands[1].kind == Z80OperandKind::Indexed)
			{
				auto target = reg8(0);
				if (target && *target != 6)
				{
					uint8_t prefix = operands[1].name == "ix" ? 0xDD : 0xFD;
					out = { prefix, uint8_t(0x46 + *target * 8), displacement(1) };
					return true;
				}
			}
			if (operands[0].kind == Z80OperandKind::Indexed)
			{
				uint8_t prefix = operands[0].name == "ix" ? 0xDD : 0xFD;
				if (auto source = reg8(1); source && *source != 6)
					out = { prefix, uint8_t(0x70 + *source), displacement(0) };
				else if (immediate(1))
					out = { prefix, 0x36, displacement(0), byte(1) };
				else
					return false;
				return true;
			}
		}

		if (auto destination = reg16(0); destination && immediate(1))
		{
			out = { uint8_t(0x01 + *destination * 0x10) };
			appendWord(out, value(1));
			return true;
		}
		if (mode == Z80Mode::Standard)
		{
			if (auto destination = reg16(0); destination && indirectImmediate(1))
			{
				if (*destination == 2) out = { 0x2A };
				else out = { 0xED, uint8_t(0x4B + *destination * 0x10) };
				appendWord(out, value(1));
				return true;
			}
			if (indirectImmediate(0))
			{
				if (auto source = reg16(1))
				{
					if (*source == 2) out = { 0x22 };
					else out = { 0xED, uint8_t(0x43 + *source * 0x10) };
					appendWord(out, value(0));
					return true;
				}
			}
		}

		auto destination8 = reg8(0);
		auto source8 = reg8(1);
		if (destination8 && source8)
		{
			if (*destination8 == 6 && *source8 == 6)
				return fail("LD (HL),(HL) is invalid");
			out = { uint8_t(0x40 + *destination8 * 8 + *source8) };
			return true;
		}
		if (destination8 && immediate(1))
		{
			out = { uint8_t(0x06 + *destination8 * 8), byte(1) };
			return true;
		}
		if (indirectName(0, "bc") && name(1, "a")) { out = { 0x02 }; return true; }
		if (indirectName(0, "de") && name(1, "a")) { out = { 0x12 }; return true; }
		if (name(0, "a") && indirectName(1, "bc")) { out = { 0x0A }; return true; }
		if (name(0, "a") && indirectName(1, "de")) { out = { 0x1A }; return true; }

		if (indirectImmediate(0) && name(1, "a"))
		{
			out = { uint8_t(mode == Z80Mode::Sm83 ? 0xEA : 0x32) };
			appendWord(out, value(0));
			return true;
		}
		if (name(0, "a") && indirectImmediate(1))
		{
			out = { uint8_t(mode == Z80Mode::Sm83 ? 0xFA : 0x3A) };
			appendWord(out, value(1));
			return true;
		}
		return false;
	}

	bool encodeIncDec(Bytes& out)
	{
		if ((mnemonic != "inc" && mnemonic != "dec") || !count(1))
			return false;
		bool increment = mnemonic == "inc";
		if (mode == Z80Mode::Standard)
		{
			if (auto prefix = indexPrefix(0))
			{
				out = { *prefix, uint8_t(increment ? 0x23 : 0x2B) };
				return true;
			}
			if (operands[0].kind == Z80OperandKind::Indexed)
			{
				out = { uint8_t(operands[0].name == "ix" ? 0xDD : 0xFD),
					uint8_t(increment ? 0x34 : 0x35), displacement(0) };
				return true;
			}
		}
		if (auto target = reg8(0))
		{
			out = { uint8_t((increment ? 0x04 : 0x05) + *target * 8) };
			return true;
		}
		if (auto target = reg16(0))
		{
			out = { uint8_t((increment ? 0x03 : 0x0B) + *target * 0x10) };
			return true;
		}
		return false;
	}

	bool encodeStack(Bytes& out)
	{
		if ((mnemonic != "push" && mnemonic != "pop") || !count(1))
			return false;
		bool push = mnemonic == "push";
		if (mode == Z80Mode::Standard)
		{
			if (auto prefix = indexPrefix(0))
			{
				out = { *prefix, uint8_t(push ? 0xE5 : 0xE1) };
				return true;
			}
		}
		if (operands[0].kind != Z80OperandKind::Name)
			return false;
		auto it = stack.find(operands[0].name);
		if (it == stack.end())
			return false;
		out = { uint8_t((push ? 0xC5 : 0xC1) + it->second * 0x10) };
		return true;
	}

	bool encodeAlu(Bytes& out)
	{
		if (mnemonic == "add" && count(2))
		{
			if (name(0, "hl"))
			{
				if (auto source = reg16(1))
				{
					out = { uint8_t(0x09 + *source * 0x10) };
					return true;
				}
			}
			if (mode == Z80Mode::Standard)
			{
				if (auto prefix = indexPrefix(0))
				{
					int source = -1;
					if (auto source16 = reg16(1)) source = *source16;
					if (operands[1].kind == Z80OperandKind::Name && operands[1].name == operands[0].name) source = 2;
					if ((source >= 0 && source != 2) ||
						(source == 2 && operands[1].name == operands[0].name))
					{
						out = { *prefix, uint8_t(0x09 + source * 0x10) };
						return true;
					}
				}
			}
			else if (name(0, "sp") && immediate(1))
			{
				out = { 0xE8, byte(1) };
				return true;
			}
		}

		if ((mnemonic == "adc" || mnemonic == "sbc") && count(2) && name(0, "hl") &&
			mode == Z80Mode::Standard)
		{
			if (auto source = reg16(1))
			{
				out = { 0xED, uint8_t((mnemonic == "adc" ? 0x4A : 0x42) + *source * 0x10) };
				return true;
			}
		}

		static const std::map<std::string, std::pair<int, int>> alu = {
			{ "add", { 0x80, 0xC6 } }, { "adc", { 0x88, 0xCE } },
			{ "sub", { 0x90, 0xD6 } }, { "sbc", { 0x98, 0xDE } },
			{ "and", { 0xA0, 0xE6 } }, { "xor", { 0xA8, 0xEE } },
			{ "or",  { 0xB0, 0xF6 } }, { "cp",  { 0xB8, 0xFE } },
		};
		auto operation = alu.find(mnemonic);
		if (operation == alu.end())
			return false;
		size_t sourceIndex = 0;
		if (count(2) && name(0, "a")) sourceIndex = 1;
		else if (!count(1)) return false;
		if (auto source = reg8(sourceIndex))
		{
			out = { uint8_t(operation->second.first + *source) };
			return true;
		}
		if (mode == Z80Mode::Standard && operands[sourceIndex].kind == Z80OperandKind::Indexed)
		{
			out = { uint8_t(operands[sourceIndex].name == "ix" ? 0xDD : 0xFD),
				uint8_t(operation->second.first + 6), displacement(sourceIndex) };
			return true;
		}
		if (immediate(sourceIndex))
		{
			out = { uint8_t(operation->second.second), byte(sourceIndex) };
			return true;
		}
		return false;
	}

	bool encodeBits(Bytes& out)
	{
		if (mnemonic == "swap" && mode == Z80Mode::Sm83)
		{
			if (!count(1)) return false;
			if (auto target = reg8(0)) { out = { 0xCB, uint8_t(0x30 + *target) }; return true; }
			return false;
		}
		auto shift = shifts.find(mnemonic);
		if (shift != shifts.end())
		{
			if (!count(1) || (mode == Z80Mode::Sm83 && mnemonic == "sll")) return false;
			if (auto target = reg8(0))
			{
				out = { 0xCB, uint8_t(shift->second * 8 + *target) };
				return true;
			}
			if (mode == Z80Mode::Standard && operands[0].kind == Z80OperandKind::Indexed)
			{
				out = { uint8_t(operands[0].name == "ix" ? 0xDD : 0xFD), 0xCB,
					displacement(0), uint8_t(shift->second * 8 + 6) };
				return true;
			}
			return false;
		}
		if (mnemonic != "bit" && mnemonic != "res" && mnemonic != "set")
			return false;
		if (!count(2) || !immediate(0)) return false;
		bool known;
		int64_t bit = value(0, known);
		if (known && (bit < 0 || bit > 7)) return fail("bit index out of range");
		int base = mnemonic == "bit" ? 0x40 : mnemonic == "res" ? 0x80 : 0xC0;
		if (auto target = reg8(1))
		{
			out = { 0xCB, uint8_t(base + int(bit) * 8 + *target) };
			return true;
		}
		if (mode == Z80Mode::Standard && operands[1].kind == Z80OperandKind::Indexed)
		{
			out = { uint8_t(operands[1].name == "ix" ? 0xDD : 0xFD), 0xCB,
				displacement(1), uint8_t(base + int(bit) * 8 + 6) };
			return true;
		}
		return false;
	}

	bool encodeControl(Bytes& out)
	{
		if (mnemonic == "jp")
		{
			if (count(1))
			{
				if (indirectName(0, "hl")) { out = { 0xE9 }; return true; }
				if (mode == Z80Mode::Standard && (indirectName(0, "ix") || indirectName(0, "iy")))
				{
					out = { uint8_t(operands[0].name == "ix" ? 0xDD : 0xFD), 0xE9 };
					return true;
				}
				if (immediate(0)) { opcodeWord(out, 0xC3, 0); return true; }
			}
			if (count(2))
			{
				if (auto cond = condition(0); cond && immediate(1))
				{
					opcodeWord(out, uint8_t(0xC2 + *cond * 8), 1);
					return true;
				}
			}
			return false;
		}
		if (mnemonic == "jr")
		{
			if (count(1) && immediate(0)) { out = { 0x18, relative(0) }; return true; }
			if (count(2))
			{
				if (auto cond = condition(0); cond && *cond <= 3 && immediate(1))
				{
					out = { uint8_t(0x20 + *cond * 8), relative(1) };
					return true;
				}
			}
			return false;
		}
		if (mnemonic == "djnz" && mode == Z80Mode::Standard && count(1) && immediate(0))
		{
			out = { 0x10, relative(0) };
			return true;
		}
		if (mnemonic == "call")
		{
			if (count(1) && immediate(0)) { opcodeWord(out, 0xCD, 0); return true; }
			if (count(2))
			{
				if (auto cond = condition(0); cond && immediate(1))
				{
					opcodeWord(out, uint8_t(0xC4 + *cond * 8), 1);
					return true;
				}
			}
			return false;
		}
		if (mnemonic == "ret" && count(1))
		{
			if (auto cond = condition(0)) { out = { uint8_t(0xC0 + *cond * 8) }; return true; }
			return false;
		}
		if (mnemonic == "rst" && count(1) && immediate(0))
		{
			bool known;
			int64_t vector = value(0, known);
			if (known && (vector < 0 || vector > 0x38 || vector % 8 != 0))
				return fail("RST vector must be $00,$08,...,$38");
			out = { uint8_t(0xC7 + vector) };
			return true;
		}
		return false;
	}

	bool encodeIo(Bytes& out)
	{
		if (mode != Z80Mode::Standard)
			return false;
		if (mnemonic == "ex" && count(2))
		{
			if (name(0, "de") && name(1, "hl")) { out = { 0xEB }; return true; }
			if (indirectName(0, "sp") && name(1, "hl")) { out = { 0xE3 }; return true; }
			if (indirectName(0, "sp") && (name(1, "ix") || name(1, "iy")))
			{
				out = { uint8_t(operands[1].name == "ix" ? 0xDD : 0xFD), 0xE3 };
				return true;
			}
		}
		if (mnemonic == "out" && count(2) && name(1, "a"))
		{
			if (indirectName(0, "c")) { out = { 0xED, 0x79 }; return true; }
			if (indirectImmediate(0)) { out = { 0xD3, byte(0) }; return true; }
		}
		if (mnemonic == "in" && count(2) && name(0, "a"))
		{
			if (indirectName(1, "c")) { out = { 0xED, 0x78 }; return true; }
			if (indirectImmediate(1)) { out = { 0xDB, byte(1) }; return true; }
		}
		return false;
	}

	std::string mnemonic;
	std::vector<Z80Operand>& operands;
	Z80Mode mode;
	int64_t pc;
	bool unresolved = false;
	std::string error;
};
}

CZ80Instruction::CZ80Instruction(std::string mnemonic, std::vector<Z80Operand> operands, Z80Mode mode)
	: mnemonic(std::move(mnemonic)), operands(std::move(operands)), mode(mode)
{
}

bool CZ80Instruction::Validate(const ValidateState& state)
{
	ramPos = g_fileManager->getVirtualAddress();
	Encoder encoder(mnemonic, operands, mode, ramPos);
	encoded = encoder.run();
	if (!encoder.getError().empty())
		Logger::queueError(Logger::Error, "%s", encoder.getError());
	if (!encoded.empty())
		g_fileManager->advanceMemory(encoded.size());
	return encoder.needsRevalidation();
}

void CZ80Instruction::Encode() const
{
	if (!encoded.empty())
		g_fileManager->write((void*)encoded.data(), encoded.size());
}

void CZ80Instruction::writeTempData(TempData& tempData) const
{
	tempData.writeLine(ramPos, mnemonic);
}
