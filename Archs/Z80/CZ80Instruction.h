#pragma once

#include "Archs/Z80/Z80.h"
#include "Commands/CAssemblerCommand.h"
#include "Core/Expression.h"

#include <string>
#include <vector>

enum class Z80OperandKind
{
	Name,
	Immediate,
	IndirectName,
	IndirectImmediate,
	Indexed,
};

struct Z80Operand
{
	Z80OperandKind kind = Z80OperandKind::Name;
	std::string name;
	Expression expression;
};

class CZ80Instruction: public CAssemblerCommand
{
public:
	CZ80Instruction(std::string mnemonic, std::vector<Z80Operand> operands, Z80Mode mode);

	bool Validate(const ValidateState& state) override;
	void Encode() const override;
	void writeTempData(TempData& tempData) const override;

private:
	std::string mnemonic;
	std::vector<Z80Operand> operands;
	Z80Mode mode;
	int64_t ramPos = 0;
	std::vector<uint8_t> encoded;
};
