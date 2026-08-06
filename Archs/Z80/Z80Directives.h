#pragma once

#include "Commands/CAssemblerCommand.h"
#include "Core/Expression.h"

#include <vector>

class Parser;

std::unique_ptr<CAssemblerCommand> parseZ80BankDirective(Parser& parser, int flags);
std::unique_ptr<CAssemblerCommand> parseZ80ExpectDirective(Parser& parser, int flags);
std::unique_ptr<CAssemblerCommand> parseZ80HeaderDirective(Parser& parser, int flags);

class CZ80BankCommand: public CAssemblerCommand
{
public:
	explicit CZ80BankCommand(Expression expression);
	bool Validate(const ValidateState& state) override;
	void Encode() const override;
	void writeTempData(TempData& tempData) const override;
private:
	Expression expression;
	int bank = 0;
	int64_t position = 0;
};

class CZ80ExpectCommand: public CAssemblerCommand
{
public:
	explicit CZ80ExpectCommand(std::vector<Expression> expressions);
	bool Validate(const ValidateState& state) override;
	void Encode() const override { }
	void writeTempData(TempData& tempData) const override;
private:
	std::vector<Expression> expressions;
	std::vector<uint8_t> expected;
	int64_t position = 0;
};

class CZ80HeaderCommand: public CAssemblerCommand
{
public:
	bool Validate(const ValidateState& state) override;
	void Encode() const override;
	void writeTempData(TempData& tempData) const override;
private:
	int64_t position = 0;
};
