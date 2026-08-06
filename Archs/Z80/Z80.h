#pragma once

#include "Archs/Architecture.h"
#include "Util/FileSystem.h"

enum class Z80Mode
{
	Standard,
	Sm83,
};

enum class Z80Platform
{
	None,
	GameBoy,
	Sms,
	GameGear,
};

class CZ80Architecture: public Architecture
{
public:
	std::unique_ptr<CAssemblerCommand> parseOpcode(Parser& parser) override;
	std::unique_ptr<CAssemblerCommand> parseDirective(Parser& parser) override;
	void NextSection() override { }
	void Pass2() override { }
	void Revalidate() override { }
	bool seekVirtualAddress(int64_t address) override;
	void onFileClose(const fs::path& fileName) override;
	std::unique_ptr<IElfRelocator> getElfRelocator() override;
	Endianness getEndianness() override { return Endianness::Little; }
	int getWordSize() override { return 2; }

	void setMode(Z80Mode value) { mode = value; }
	Z80Mode getMode() const { return mode; }
	void setPlatform(Z80Platform value) { platform = value; }
	Z80Platform getPlatform() const { return platform; }
	void setBank(int value) { bank = value; bankingEnabled = true; }
	int getBank() const { return bank; }
	bool isBankingEnabled() const { return bankingEnabled; }
	void resetBanking();
	bool seekBanked(int64_t address) const;
	void requestHeader(const fs::path& fileName);
	void clear();

private:
	Z80Mode mode = Z80Mode::Standard;
	Z80Platform platform = Z80Platform::None;
	int bank = 0;
	bool bankingEnabled = false;
	std::map<fs::path, Z80Platform> pendingHeaders;
};

extern CZ80Architecture Z80;

class CZ80ArchitectureCommand: public ArchitectureCommand
{
public:
	CZ80ArchitectureCommand(const std::string& text, Z80Mode mode, Z80Platform platform);
	bool Validate(const ValidateState& state) override;
	void Encode() const override;

private:
	void activate() const;

	Z80Mode mode;
	Z80Platform platform;
};
