#include "Archs/Z80/Z80.h"

#include "Archs/Z80/Z80Parser.h"
#include "Archs/Z80/Z80Directives.h"
#include "Core/Common.h"
#include "Core/FileManager.h"
#include "Core/ELF/ElfRelocator.h"
#include "Core/Misc.h"
#include "Parser/DirectivesParser.h"
#include "Parser/Parser.h"

#include <fstream>

CZ80Architecture Z80;

namespace
{
const DirectiveMap z80Directives = {
	{ ".bank",   { &parseZ80BankDirective, 0 } },
	{ ".expect", { &parseZ80ExpectDirective, 0 } },
	{ ".header", { &parseZ80HeaderDirective, 0 } },
};

uint16_t segaChecksum(const std::vector<uint8_t>& rom)
{
	uint32_t checksum = 0;
	for (size_t index = 0; index < rom.size(); index++)
	{
		if (index >= 0x7FF0 && index < 0x8000)
			continue;
		checksum += rom[index];
	}
	return uint16_t(checksum);
}
}

std::unique_ptr<CAssemblerCommand> CZ80Architecture::parseOpcode(Parser& parser)
{
	Z80Parser z80Parser;
	return z80Parser.parseOpcode(parser);
}

std::unique_ptr<CAssemblerCommand> CZ80Architecture::parseDirective(Parser& parser)
{
	return parser.parseDirective(z80Directives);
}

std::unique_ptr<IElfRelocator> CZ80Architecture::getElfRelocator()
{
	return nullptr;
}

void CZ80Architecture::clear()
{
	mode = Z80Mode::Standard;
	platform = Z80Platform::None;
	resetBanking();
	pendingHeaders.clear();
}

void CZ80Architecture::resetBanking()
{
	bank = 0;
	bankingEnabled = false;
}

bool CZ80Architecture::seekBanked(int64_t address) const
{
	auto openFile = g_fileManager->getOpenFile();
	if (!openFile || !openFile->hasFixedVirtualAddress())
	{
		Logger::printError(Logger::Error, ".bank requires an open generic file");
		return false;
	}
	if (bank < 0)
	{
		Logger::printError(Logger::Error, "Negative ROM bank");
		return false;
	}

	if (platform == Z80Platform::GameBoy)
	{
		if ((bank == 0 && (address < 0 || address > 0x3FFF)) ||
			(bank != 0 && (address < 0x4000 || address > 0x7FFF)))
		{
			Logger::printError(Logger::Error, "GB bank %d cannot map CPU address 0x%04llX", bank, address);
			return false;
		}
	}
	else if (platform == Z80Platform::Sms || platform == Z80Platform::GameGear)
	{
		if (address < 0 || address > 0xBFFF)
		{
			Logger::printError(Logger::Error, "Sega bank %d cannot map CPU address 0x%04llX", bank, address);
			return false;
		}
	}
	else
	{
		return g_fileManager->seekVirtual(address);
	}

	int64_t physical = int64_t(bank) * 0x4000 + (address & 0x3FFF);
	auto file = std::static_pointer_cast<GenericAssemblerFile>(openFile);
	file->setHeaderSize(address - physical);
	file->setPhysicalAddressLimit((int64_t(bank) + 1) * 0x4000);
	return file->seekVirtual(address);
}

bool CZ80Architecture::seekVirtualAddress(int64_t address)
{
	return bankingEnabled ? seekBanked(address) : g_fileManager->seekVirtual(address);
}

void CZ80Architecture::requestHeader(const fs::path& fileName)
{
	pendingHeaders[fileName] = platform;
}

void CZ80Architecture::onFileClose(const fs::path& fileName)
{
	auto pending = pendingHeaders.find(fileName);
	if (pending == pendingHeaders.end())
		return;
	Z80Platform target = pending->second;
	pendingHeaders.erase(pending);

	std::ifstream input(fileName, std::ios::binary);
	if (!input)
	{
		Logger::printError(Logger::Error, "Could not read output for .header: %s", fileName.u8string());
		return;
	}
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	input.close();

	if (target == Z80Platform::GameBoy)
	{
		static const std::map<size_t, uint8_t> sizes = {
			{ 32 * 1024, 0 }, { 64 * 1024, 1 }, { 128 * 1024, 2 },
			{ 256 * 1024, 3 }, { 512 * 1024, 4 }, { 1024 * 1024, 5 },
			{ 2048 * 1024, 6 }, { 4096 * 1024, 7 }, { 8192 * 1024, 8 },
		};
		auto size = sizes.find(rom.size());
		if (rom.size() < 0x150 || size == sizes.end())
		{
			Logger::printError(Logger::Error, "Unsupported GB ROM size for .header: %zu", rom.size());
			return;
		}
		rom[0x148] = size->second;
		uint8_t header = 0;
		for (size_t index = 0x134; index <= 0x14C; index++)
			header = uint8_t(header - rom[index] - 1);
		rom[0x14D] = header;
		uint32_t global = 0;
		for (size_t index = 0; index < rom.size(); index++)
			if (index != 0x14E && index != 0x14F) global += rom[index];
		rom[0x14E] = uint8_t(global >> 8);
		rom[0x14F] = uint8_t(global);
	}
	else
	{
		static const std::map<size_t, uint8_t> sizes = {
			{ 8 * 1024, 0xA }, { 16 * 1024, 0xB }, { 32 * 1024, 0xC },
			{ 48 * 1024, 0xD }, { 64 * 1024, 0xE }, { 128 * 1024, 0xF },
			{ 256 * 1024, 0 }, { 512 * 1024, 1 }, { 1024 * 1024, 2 },
		};
		auto size = sizes.find(rom.size());
		if (rom.size() < 0x8000 || size == sizes.end() ||
			std::string(rom.begin() + 0x7FF0, rom.begin() + 0x7FF8) != "TMR SEGA")
		{
			Logger::printError(Logger::Error, "Invalid SMS/GG ROM for .header");
			return;
		}
		rom[0x7FFF] = uint8_t((rom[0x7FFF] & 0xF0) | size->second);
		uint16_t checksum = segaChecksum(rom);
		rom[0x7FFA] = uint8_t(checksum);
		rom[0x7FFB] = uint8_t(checksum >> 8);
	}

	std::ofstream output(fileName, std::ios::binary | std::ios::trunc);
	output.write(reinterpret_cast<const char*>(rom.data()), std::streamsize(rom.size()));
	if (!output)
		Logger::printError(Logger::Error, "Could not write .header output: %s", fileName.u8string());
}

CZ80ArchitectureCommand::CZ80ArchitectureCommand(
	const std::string& text, Z80Mode mode, Z80Platform platform)
	: ArchitectureCommand(text, ""), mode(mode), platform(platform)
{
}

void CZ80ArchitectureCommand::activate() const
{
	Z80.setMode(mode);
	Z80.setPlatform(platform);
	Z80.resetBanking();
}

bool CZ80ArchitectureCommand::Validate(const ValidateState& state)
{
	bool result = ArchitectureCommand::Validate(state);
	activate();
	return result;
}

void CZ80ArchitectureCommand::Encode() const
{
	ArchitectureCommand::Encode();
	activate();
}
