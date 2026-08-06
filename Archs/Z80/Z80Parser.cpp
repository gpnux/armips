#include "Archs/Z80/Z80Parser.h"

#include "Archs/Z80/Z80.h"
#include "Parser/Parser.h"

#include <algorithm>
#include <set>

namespace
{
const std::set<std::string> mnemonics = {
	"adc", "add", "and", "bit", "call", "ccf", "cp", "cpl", "daa",
	"dec", "di", "djnz", "ei", "ex", "exx", "halt", "in", "inc",
	"jp", "jr", "ld", "ldh", "ldir", "nop", "or", "out", "pop",
	"push", "res", "ret", "reti", "retn", "rl", "rla", "rlc", "rlca",
	"rr", "rra", "rrc", "rrca", "rst", "sbc", "scf", "set", "sla",
	"sll", "sra", "srl", "stop", "sub", "swap", "xor",
};

const std::set<std::string> names = {
	"a", "af", "b", "bc", "c", "d", "de", "e", "h", "hl", "i",
	"ix", "iy", "l", "m", "nc", "nz", "p", "pe", "po", "r", "sp", "z",
};

std::string identifierText(const Token& token)
{
	return token.identifierValue().string();
}
}

bool Z80Parser::parseOperand(Parser& parser, Z80Operand& operand)
{
	if (parser.peekToken().type == TokenType::LParen)
	{
		parser.eatToken();
		if (parser.peekToken().type == TokenType::Identifier)
		{
			std::string name = identifierText(parser.peekToken());
			if (name == "hl" && parser.peekToken(1).type == TokenType::Plus &&
				parser.peekToken(2).type == TokenType::RParen)
			{
				parser.eatTokens(3);
				operand.kind = Z80OperandKind::IndirectName;
				operand.name = "hl+";
				return true;
			}
			if (name == "hl" && parser.peekToken(1).type == TokenType::Minus &&
				parser.peekToken(2).type == TokenType::RParen)
			{
				parser.eatTokens(3);
				operand.kind = Z80OperandKind::IndirectName;
				operand.name = "hl-";
				return true;
			}
			if ((name == "ix" || name == "iy") &&
				(parser.peekToken(1).type == TokenType::Plus ||
				 parser.peekToken(1).type == TokenType::Minus))
			{
				operand.kind = Z80OperandKind::Indexed;
				operand.name = name;
				parser.eatToken();
				operand.expression = parser.parseExpression();
				if (!operand.expression.isLoaded() || !parser.matchToken(TokenType::RParen))
					return false;
				return true;
			}
			if (names.count(name) != 0 && parser.peekToken(1).type == TokenType::RParen)
			{
				parser.eatTokens(2);
				operand.kind = Z80OperandKind::IndirectName;
				operand.name = name;
				return true;
			}
		}

		operand.expression = parser.parseExpression();
		if (!operand.expression.isLoaded() || !parser.matchToken(TokenType::RParen))
			return false;
		operand.kind = Z80OperandKind::IndirectImmediate;
		return true;
	}

	if (parser.peekToken().type == TokenType::Identifier)
	{
		std::string name = identifierText(parser.peekToken());
		if (names.count(name) != 0)
		{
			parser.eatToken();
			operand.kind = Z80OperandKind::Name;
			operand.name = name;
			return true;
		}
	}

	operand.expression = parser.parseExpression();
	if (!operand.expression.isLoaded())
		return false;
	operand.kind = Z80OperandKind::Immediate;
	return true;
}

std::unique_ptr<CZ80Instruction> Z80Parser::parseOpcode(Parser& parser)
{
	if (parser.peekToken().type != TokenType::Identifier)
		return nullptr;

	const Token start = parser.peekToken();
	std::string mnemonic = identifierText(start);
	if (mnemonics.count(mnemonic) == 0)
		return nullptr;
	parser.eatToken();

	std::vector<Z80Operand> operands;
	while (parser.peekToken().type != TokenType::Separator)
	{
		Z80Operand operand;
		if (!parseOperand(parser, operand))
		{
			parser.printError(start, "Invalid Z80/SM83 operand");
			return nullptr;
		}
		operands.push_back(std::move(operand));
		if (parser.peekToken().type == TokenType::Separator)
			break;
		if (!parser.matchToken(TokenType::Comma))
		{
			parser.printError(start, "Expected comma between Z80/SM83 operands");
			return nullptr;
		}
	}
	parser.eatToken();

	return std::make_unique<CZ80Instruction>(std::move(mnemonic), std::move(operands), Z80.getMode());
}
