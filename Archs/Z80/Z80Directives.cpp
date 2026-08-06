#include "Archs/Z80/Z80Directives.h"

#include "Archs/Z80/Z80.h"
#include "Core/Common.h"
#include "Core/FileManager.h"
#include "Core/Misc.h"
#include "Core/SymbolData.h"
#include "Parser/Parser.h"

#include <fstream>

std::unique_ptr<CAssemblerCommand> parseZ80BankDirective(Parser& parser, int flags)
{
	Expression expression = parser.parseExpression();
	if (!expression.isLoaded())
		return nullptr;
	return std::make_unique<CZ80BankCommand>(std::move(expression));
}

std::unique_ptr<CAssemblerCommand> parseZ80ExpectDirective(Parser& parser, int flags)
{
	std::vector<Expression> expressions;
	if (!parser.parseExpressionList(expressions, 1, -1))
		return nullptr;
	return std::make_unique<CZ80ExpectCommand>(std::move(expressions));
}

std::unique_ptr<CAssemblerCommand> parseZ80HeaderDirective(Parser& parser, int flags)
{
	return std::make_unique<CZ80HeaderCommand>();
}

CZ80BankCommand::CZ80BankCommand(Expression expression): expression(std::move(expression))
{
}

bool CZ80BankCommand::Validate(const ValidateState& state)
{
	position = g_fileManager->getVirtualAddress();
	if (!expression.evaluateInteger(bank) || bank < 0)
	{
		Logger::queueError(Logger::Error, "Invalid ROM bank");
		return false;
	}
	Z80.setBank(bank);
	return false;
}

void CZ80BankCommand::Encode() const
{
	Z80.setBank(bank);
}

void CZ80BankCommand::writeTempData(TempData& tempData) const
{
	tempData.writeLine(position, tfm::format(".bank 0x%X", bank));
}

CZ80ExpectCommand::CZ80ExpectCommand(std::vector<Expression> expressions)
	: expressions(std::move(expressions))
{
}

bool CZ80ExpectCommand::Validate(const ValidateState& state)
{
	position = g_fileManager->getVirtualAddress();
	expected.clear();
	for (Expression& expression: expressions)
	{
		int value;
		if (!expression.evaluateInteger(value) || value < 0 || value > 0xFF)
		{
			Logger::queueError(Logger::Error, ".expect value must be an 8-bit integer");
			return false;
		}
		expected.push_back(uint8_t(value));
	}

	auto openFile = g_fileManager->getOpenFile();
	if (!openFile || !openFile->hasFixedVirtualAddress())
	{
		Logger::queueError(Logger::Error, ".expect requires an open generic file");
		return false;
	}
	auto file = std::static_pointer_cast<GenericAssemblerFile>(openFile);
	const fs::path& source = file->getOriginalFileName().empty()
		? file->getFileName() : file->getOriginalFileName();
	std::ifstream input(source, std::ios::binary);
	if (!input)
	{
		Logger::queueError(Logger::Error, "Could not read .expect source %s", source.u8string());
		return false;
	}
	input.seekg(g_fileManager->getPhysicalAddress());
	std::vector<uint8_t> actual(expected.size());
	input.read(reinterpret_cast<char*>(actual.data()), std::streamsize(actual.size()));
	if (input.gcount() != std::streamsize(actual.size()) || actual != expected)
	{
		Logger::queueError(Logger::Error, ".expect failed at ROM offset 0x%llX",
			g_fileManager->getPhysicalAddress());
	}
	return false;
}

void CZ80ExpectCommand::writeTempData(TempData& tempData) const
{
	std::string line = ".expect ";
	for (size_t index = 0; index < expected.size(); index++)
	{
		if (index != 0) line += ", ";
		line += tfm::format("0x%02X", expected[index]);
	}
	tempData.writeLine(position, line);
}

bool CZ80HeaderCommand::Validate(const ValidateState& state)
{
	position = g_fileManager->getVirtualAddress();
	if (Z80.getPlatform() == Z80Platform::None)
		Logger::queueError(Logger::Error, ".header requires .gb, .sms, or .gg");
	return false;
}

void CZ80HeaderCommand::Encode() const
{
	auto file = g_fileManager->getOpenFile();
	if (file)
		Z80.requestHeader(file->getFileName());
}

void CZ80HeaderCommand::writeTempData(TempData& tempData) const
{
	tempData.writeLine(position, ".header");
}
