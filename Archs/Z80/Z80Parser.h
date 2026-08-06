#pragma once

#include "Archs/Z80/CZ80Instruction.h"

#include <memory>

class Parser;

class Z80Parser
{
public:
	std::unique_ptr<CZ80Instruction> parseOpcode(Parser& parser);

private:
	bool parseOperand(Parser& parser, Z80Operand& operand);
};
