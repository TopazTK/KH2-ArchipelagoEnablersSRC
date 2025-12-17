#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <chrono>
#include <format>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <Windows.h>
#include <algorithm>

#include "ini.h"
#include "memory_tools.h"

#include "area.h"
#include "command_draw.h"
#include "egs.h"
#include "event.h"
#include "file.h"
#include "gauge.h"
#include "hardpad.h"
#include "information.h"
#include "jumpeffect.h"
#include "magic.h"
#include "member_table.h"
#include "menu.h"
#include "message.h"
#include "pax.h"
#include "softreset.h"
#include "sora.h"
#include "sound.h"
#include "steam.h"
#include "title.h"
#include "treasure_info.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

using namespace std;

bool IS_DEAD;

bool IS_INIT = false;
bool IS_RESETING = false;

bool SYSTEM_LOADED = false;
bool SAVE_INITIATE = false;
bool SYSTEM_WRITTEN = false;

bool ROUND_BACK = false;

YS::AREA::INFO SAVE_AREA;
int SAVE_ITERATOR = 0;

vector<uint32_t> CHECKSUM_TABLE;

uint8_t IS_STEAM = FindModule("steam_api64.dll");
uint8_t SAVE_CHECK = 0xEB;

wstring MOD_PATH;

uint16_t RESET_COMBO = 0x0000;

uint8_t ROOM_AMOUNT = 1;
uint8_t SAVE_SLOT_OFFSET = 99;

uint32_t MAGIC_FIRST;
uint16_t MAGIC_SECOND;

map<uint32_t, char*> MAGIC_FILES;

vector<uint16_t> ABILITY_ARRAY;

void TriggerReset()
{
	// Fetch the input and whether a soft reset can happen.
	auto _fetchButtons = *YS::HARDPAD::Input;
	auto _commandPointer = *reinterpret_cast<const char**>(YS::COMMAND_DRAW::pint_commanddraw);

	bool _canReset = _commandPointer != 0x00 && *YS::AREA::IsInMap && !*YS::TITLE::IsTitle && !*YS::MENU::IsMenu && RESET_COMBO != 0x00;

	// If the buttons are pushed, a reset can happen and it isn't happening:
	if (RESET_COMBO != YS::HARDPAD::BUTTONS::NONE && _fetchButtons == RESET_COMBO && _canReset && !IS_RESETING)
	{
		// Declare the reset is happening for timing purposes.
		IS_RESETING = true;

		// Initiate the fadeout for BGMs.
		YS::SOUND::BGMFadeOut(200, 0x00);
		YS::SOUND::BGMFadeOut(200, 0x01);

		// Initiate fade-to-black.
		dk::JUMPEFFECT::Out(0x01);
	}

	// If it's resetting, and the fade is complete:
	if (IS_RESETING && *(dk::JUMPEFFECT::FadeStatus + 0x108) == 0x04)
	{
		// Set the reset byte and declare we are no longer resetting.
		*(dk::SOFTRESET::RESET) = 0x01;
		IS_RESETING = false;
	}
}

void RegisterMagic()
{
	// Fetch the current levels or magic.

	uint32_t _magicFirst = *reinterpret_cast<const uint32_t*>(YS::AREA::SaveData + 0x3594);
	uint16_t _magicSecond = *reinterpret_cast<const uint16_t*>(YS::AREA::SaveData + 0x35CF);

	// If the game is loaded, and if the Tier 1 Magic or the Tier 2 Magic levels do not match what is previously recorded.

	if (*YS::AREA::IsInMap && (_magicFirst != MAGIC_FIRST || _magicSecond != MAGIC_SECOND))
	{
		// Initialize the array we will be using for the commands.
		vector<uint16_t> _commandArray;

		// For 6 Magic (Fire, Blizzard, Thunder, Cure, Magnet, Reflect):
		for (uint32_t i = 0; i < 0x06; i++)
		{
			// Get the current table and denote the pointer.

			auto _currentTable = YS::MAGIC::GetTable(i);
			auto _currentTablePtr = reinterpret_cast<uint64_t>(_currentTable);

			// If the current table exists (Meaning we have the Magic):

			if (_currentTable != 0x00)
			{
				// Fetch the previously denoted level of the current Magic, and fetch the current Magic table pointer.

				char _fetchMagicLevel = i <= 0x03 ? *(&MAGIC_FIRST + i) : *(&MAGIC_SECOND + (i - 0x04));
				auto _currentPointer = *reinterpret_cast<const uint64_t*>(YS::MAGIC::MagicInfo + 0x48 + 0x50 * i);

				// If the current Magic table is the one we fetched and the current Magic level is not zero (Meaning we have the Magic and it was processed), or the denoted level equals to the current level:
				if ((_currentPointer == _currentTablePtr && _fetchMagicLevel != 0x00) || _fetchMagicLevel == _currentTable->Level)
				{
					// Denote the command of the current Magic to the array, and skip to the next entry.
					_commandArray.push_back(_currentTable->Command);
					continue;
				}

				// If a previous of the Magic was processed by this function, free it and remove it from the process list.
				if (MAGIC_FILES.count(i) != 0x00)
				{
					free(MAGIC_FILES.at(i));
					MAGIC_FILES.erase(i);
				}

				// Get the size of the .mag file and allocate memory for it.
				size_t _fetchSize = YS::FILE::GetSize(_currentTable->Filename);
				auto _loadBAR = (char*)malloc(_fetchSize);

				// If the magic file can load (Meaning it exists and we have allocated the memory successfully):
				if (YS::FILE::LoadBAR(_currentTable->Filename, _loadBAR) != 0x00)
				{
					// Denote the address of the loaded file for future handling.
					MAGIC_FILES[i] = _loadBAR;

					// Get the address of the BAR as a uint64_t for calculations, and get the 32-bit offset of the BAR.
					uint64_t _barAddress = reinterpret_cast<uint64_t>(_loadBAR);
					uint32_t _barFileOffset = *reinterpret_cast<const uint32_t*>(_loadBAR + 0x08);

					// Calculate the absolute addresses of PAX and BDX files.
					uint64_t _paxAddress = _barAddress + *reinterpret_cast<const uint32_t*>(_loadBAR + 0x18) - _barFileOffset;
					uint64_t _bdxAddress = _barAddress + *reinterpret_cast<const uint32_t*>(_loadBAR + 0x28) - _barFileOffset;

					// As well as the address of which PAX actually starts in.
					uint64_t _paxStartAddress = _barAddress + *reinterpret_cast<const uint32_t*>(_loadBAR + 0x18) + 0x10 - _barFileOffset;

					// Write all of the info needed for the Magic to be parsed, processed, and executed.
					memcpy(YS::MAGIC::MagicInfo + 0x50 * i, &_barAddress, 0x08);
					memcpy(YS::MAGIC::MagicInfo + 0x08 + 0x50 * i, &_bdxAddress, 0x08);
					memcpy(YS::MAGIC::MagicInfo + 0x10 + 0x50 * i, &_bdxAddress, 0x08);
					memcpy(YS::MAGIC::MagicInfo + 0x18 + 0x50 * i, &_paxAddress, 0x08);
					memcpy(YS::MAGIC::MagicInfo + 0x20 + 0x50 * i, &_paxStartAddress, 0x08);

					// Overwrite the pointer to the current Magic table.
					memcpy(YS::MAGIC::MagicInfo + 0x48 + 0x50 * i, &_currentTablePtr, 0x08);

					// Initialize the current Magic PAX.
					ryj::PAX::Init(YS::MAGIC::MagicInfo + 0x18 + 0x50 * i, reinterpret_cast<char*>(_paxAddress));

					// Denote the command of the current Magic to the array.
					_commandArray.push_back(_currentTable->Command);
				};
			}
		}

		// Copy the commands array to where the Magic Commands are stored.
		_commandArray.resize(0x06);
		memcpy(YS::MAGIC::MagicCommands, _commandArray.data(), 0x0C);

		// Denote the current Magic levels for both tiers of Magic.
		MAGIC_FIRST = _magicFirst;
		MAGIC_SECOND = _magicSecond;
	}

	// If the game isn't loaded, free every single magic that we have processed ourselves.
	else if (!*YS::AREA::IsInMap)
	{
		for (auto _magicEntry : MAGIC_FILES)
			free(_magicEntry.second);

		MAGIC_FILES.clear();
	}
}

void RegisterMovement()
{
	// Fetch the presence of Sora's Gauge [Edge Case for 100 Acre Woods minigames.]
	auto _soraGauge = CalculatePointer(dk::GAUGE::pint_playergauge, { 0x88, 0x00 });

	// See if there is specifically a Cutscene playing.
	auto _eventPointer = *reinterpret_cast<const char**>(YS::EVENT::pint_eventinfo);

	auto _fetchEvent = CalculatePointer(YS::EVENT::pint_eventinfo, { 0x04 });
	bool _isCutscene = _fetchEvent != 0x00 && *reinterpret_cast<const uint32_t*>(_fetchEvent) != 0xCAFEEFAC &&
		*reinterpret_cast<const uint32_t*>(_fetchEvent) != 0xEFACCAFE;

	auto _commandPointer = *reinterpret_cast<const char**>(YS::COMMAND_DRAW::pint_commanddraw);

	// If the game is loaded:
	if (*YS::AREA::IsInMap && _commandPointer != 0x00 && _soraGauge != 0x00 && !_isCutscene && _eventPointer == 0x00)
	{
		// If  the ability denotation is not initialized:
		if (ABILITY_ARRAY.size() == 0x00)
		{
			// Resize the denotation to be 0x60 elements.
			ABILITY_ARRAY.resize(0x60);

			// Denote all of Sora's current abilities and sort them.
			memcpy(ABILITY_ARRAY.data(), YS::AREA::SaveData + 0x2544, 0xC0);
			sort(ABILITY_ARRAY.begin(), ABILITY_ARRAY.end());
		}

		// Create the vectors for current ability calculations.
		vector<uint16_t> _abilityDiff;
		vector<uint16_t> _currentAbility(0x60);

		// Denote all of Sora's current abilities and sort them.
		memcpy(_currentAbility.data(), YS::AREA::SaveData + 0x2544, 0xC0);
		sort(_currentAbility.begin(), _currentAbility.end());

		// Get all the abilities that differ between the old and current denotation.
		set_difference(_currentAbility.begin(), _currentAbility.end(), ABILITY_ARRAY.begin(), ABILITY_ARRAY.end(), inserter(_abilityDiff, _abilityDiff.begin()));

		// Check if any of the different abilities contain movement.
		bool _fetchMovement = any_of(_abilityDiff.begin(), _abilityDiff.end(), [](int x) {
			return (x >= 0x805E && x <= 0x806D) || (x >= 0x8234 && x <= 0x8237) || (x == 0x0194 || x == 0x8194);
			});

		// If they do:
		if (_fetchMovement)
		{
			// Refresh all of Sora's stats, which in turn, will commit all movement changes.
			YS::SORA::RefreshAbilities(YS::MEMBER_TABLE::MemberStatsAnchor + 0xC308 + 0x1D0);
		}

		// Copy over the current ability list to the denotation array.
		ABILITY_ARRAY.assign(_currentAbility.begin(), _currentAbility.end());
	}
}

void ShowInformation()
{
	// Fetch the presence of Sora's Gauge [Edge Case for 100 Acre Woods minigames.]
	auto _soraGauge = CalculatePointer(dk::GAUGE::pint_playergauge, { 0x88, 0x00 });

	auto _commandPointer = *reinterpret_cast<const char**>(YS::COMMAND_DRAW::pint_commanddraw);

	// See if there is specifically a Cutscene playing.
	auto _eventPointer = *reinterpret_cast<const char**>(YS::EVENT::pint_eventinfo);

	auto _fetchEvent = CalculatePointer(YS::EVENT::pint_eventinfo, { 0x04 });
	bool _isCutscene = _eventPointer != nullptr && _fetchEvent != 0x00 && *reinterpret_cast<const uint32_t*>(_fetchEvent) != 0xCAFEEFAC &&
		*reinterpret_cast<const uint32_t*>(_fetchEvent) != 0xEFACCAFE;

	// If the game is loaded, and there isn't a menu present, and it's not a cutscene:
	if (*YS::AREA::IsInMap && _commandPointer != 0x00 && !*YS::MENU::IsMenu && !_isCutscene && _soraGauge != 0x00 && _eventPointer == 0x00)
	{
		// Fetch the fade status and the enable line.
		auto _fetchFade = *(dk::JUMPEFFECT::FadeStatus + 0x108);
		auto _fetchEnable = moduleInfo.startAddr[0x800000];

		// If there is no fade, and the enable line is set:
		if (_fetchFade == 0x00 && _fetchEnable != 0x00)
		{
			// Reset the enable line.
			*const_cast<char*>(moduleInfo.startAddr + 0x800000) = 0x00;

			// If the enable line is 0x01, summon INFORMATION. If it's 0x02, summon PRIZE.
			switch (_fetchEnable)
			{
			case 0x01:
				dk::INFORMATION::openInformationWindow(moduleInfo.startAddr + 0x800004);
				break;

			case 0x02:
				dk::TREASURE_INFO::openPrizeWindow(moduleInfo.startAddr + 0x800104);
				break;

			case 0x03:
				dk::TREASURE_INFO::openBoxWindow(moduleInfo.startAddr + 0x800154, *reinterpret_cast<const uint16_t*>(moduleInfo.startAddr + 0x800150));
			}
		}
	}
}

void ProcessDeath()
{
	// Fetch the presence of Sora's Gauge [Edge Case for 100 Acre Woods minigames.]
	auto _soraGauge = CalculatePointer(dk::GAUGE::pint_playergauge, { 0x88, 0x00 });

	// Fetch Sora's pointer as well as his UCM.
	auto _soraSelf = *reinterpret_cast<const uint64_t*>(YS::SORA::pint_sora);
	auto _fetchSora = *reinterpret_cast<const uint16_t*>(YS::MEMBER_TABLE::MemberTable);

	// If Sora's HP is 0, and he isn't Mermaid Sora, and his gauge is present, and he isn't dead:
	if (*(YS::MEMBER_TABLE::MemberStatsAnchor + 0xC308) == 0x00 && *YS::AREA::IsInMap && !*YS::MENU::IsMenu && (_fetchSora != 0x03BE && _fetchSora != 0x0656) && _soraGauge != 0x00 && !IS_DEAD)
	{
		// Process his death and mark it.
		YS::SORA::AddHP(reinterpret_cast<char*>(_soraSelf), 0x00, 0x00, false);
		IS_DEAD = true;
	}

	// If Sora's HP is NOT 0 but he is dead, mark him as not.
	else if (*(YS::MEMBER_TABLE::MemberStatsAnchor + 0xC308) != 0x00 && IS_DEAD)
		IS_DEAD = false;
}

void AutosaveLogic()
{
	if (CHECKSUM_TABLE.size() == 0x00)
	{
		for (auto x = 0; x <= 0xFF; x++)
		{
			auto r = x << 24;

			for (auto j = 0; j < 0xFF; j++)
				r = r << 1 ^ (r < 0 ? 0x4C11DB7 : 0);

			CHECKSUM_TABLE.push_back(r);
		}
	}

	auto _commandPointer = *reinterpret_cast<const char**>(YS::COMMAND_DRAW::pint_commanddraw);
	const char* _gaugeTypePointer = CalculatePointer(dk::GAUGE::pint_playergauge, { 0x88 });
	const char* _mainPointer = *reinterpret_cast<const char**>(YS::EVENT::pint_eventinfo);

	uint64_t _savePointer = IS_STEAM ? PC::STEAM::pint_saveinformation : PC::EGS::pint_saveinformation;

	if (!SYSTEM_WRITTEN)
	{
		char* _systemInfo = const_cast<char*>(CalculatePointer(_savePointer, { 0x10, 0x10 }));

		const char* _systemText = "BISLPM-66675FM-SYS";
		uint32_t _systemLength = 0x400;

		if (strcmp(_systemInfo, _systemText) != 0x00)
		{
			const auto _currTime = chrono::system_clock::now();
			auto _unixTime = static_cast<time_t>(chrono::duration_cast<chrono::seconds>(_currTime.time_since_epoch()).count());

			memcpy(_systemInfo, _systemText, 0x12);

			memcpy(_systemInfo + 0x40, &_unixTime, 4);
			memcpy(_systemInfo + 0x48, &_unixTime, 4);

			memcpy(_systemInfo + 0x50, &_systemLength, 4);
		}

		SYSTEM_WRITTEN = true;
	}

	if (*YS::AREA::IsInMap && !*YS::TITLE::IsTitle && !SYSTEM_LOADED)
		SYSTEM_LOADED = true;

	else if (*YS::TITLE::IsTitle && SYSTEM_LOADED)
	{
		SAVE_ITERATOR = 0;
		SYSTEM_LOADED = false;
		SAVE_AREA = *YS::AREA::Current;
	}

	if (_commandPointer != 0x00 && _gaugeTypePointer != 0x00)
	{
		bool _checkBlacklist = YS::AREA::Current->World == 0x0F || YS::AREA::Current->World == 0x0B ||
			(YS::AREA::Current->World == 0x08 && YS::AREA::Current->Room == 0x03) ||
			(YS::AREA::Current->World == 0x0C && YS::AREA::Current->Room == 0x02) ||
			(YS::AREA::Current->World == 0x02 && YS::AREA::Current->Room <= 0x01) ||
			(YS::AREA::Current->World == 0x04 && YS::AREA::Current->Room == 0x10) ||
			(YS::AREA::Current->World == 0x12 && YS::AREA::Current->Room >= 0x13 && YS::AREA::Current->Room <= 0x1D);

		if (!*YS::TITLE::IsTitle && *YS::AREA::IsInMap && !_checkBlacklist)
		{
			if (SAVE_AREA.World == 0x00)
				SAVE_AREA = *YS::AREA::Current;

			bool _checkStatus = !*YS::MENU::IsMenu && _commandPointer != 0x00 && *YS::AREA::IsInMap && _mainPointer == 0x00 && *YS::AREA::BattleStatus == 0x00 && SAVE_AREA.World >= 0x02 && SYSTEM_LOADED && *(dk::JUMPEFFECT::FadeStatus + 0x108) == 0x00;

			if (!_checkStatus)
			{
				SAVE_INITIATE = false;
				return;
			}

			if (SAVE_AREA.World != YS::AREA::Current->World)
			{
				SAVE_INITIATE = true;
				SAVE_ITERATOR = 0;
			}

			if (SAVE_AREA.Room != YS::AREA::Current->Room)
			{
				SAVE_ITERATOR++;

				if (SAVE_ITERATOR == ROOM_AMOUNT)
				{
					SAVE_INITIATE = true;
					SAVE_ITERATOR = 0;
				}
			}

			SAVE_AREA = *YS::AREA::Current;
		}
	}

	if (SAVE_INITIATE)
	{
		auto _saveOffset = SAVE_SLOT_OFFSET;

	START_FUNC:

		ostringstream _stringStream;
		_stringStream << setw(2) << setfill('0') << (_saveOffset - 1);

		string _saveName = "BISLPM-66675FM-" + _stringStream.str();
		const char* _saveHeader = "KH2J";

		vector<string> _usedSlots;

		char* _saveFilePath = const_cast<char*>(CalculatePointer(_savePointer, { 0x40 }));
		string _saveFileString(_saveFilePath);

		_saveFileString = _saveFileString.append(IS_STEAM ? "\\KHIIFM_WW.png" : "\\KHIIFM.png");

		const auto _currTime = chrono::system_clock::now();
		auto _unixTime = static_cast<time_t>(chrono::duration_cast<chrono::seconds>(_currTime.time_since_epoch()).count());

		uint32_t _saveSlot = 0x00;
		uint32_t _saveHeaderID = 0x3A;
		uint32_t _saveInfoLength = 0x158;
		uint32_t _saveDataLength = 0x10FC0;

		uint32_t _autoSaveTag = 0xFFFFFFFF;
		uint32_t _regularSaveTag = 0x00000000;

		uint32_t _saveInfoStartFILE = 0x1C8;
		uint32_t _saveDataStartFILE = 0x19690;

		char* _saveInfoStartRAM = CalculatePointer(_savePointer, { 0x10, 0x168 });
		char* _saveDataStartRAM = CalculatePointer(_savePointer, { 0x10, 0x19630 });

		memcpy(YS::AREA::SaveData + 0x10, &_autoSaveTag, 0x04);

		const char* _saveSlotRAM = _saveInfoStartRAM + (_saveInfoLength * _saveSlot);

		for (int i = 0; i < 99; i++)
			if (*(_saveInfoStartRAM + (_saveInfoLength * i)) != 0x00)
				_usedSlots.push_back(string(_saveInfoStartRAM + (_saveInfoLength * i)));

		while (_saveSlotRAM[0] != 0x00 && strcmp(_saveSlotRAM, _saveName.c_str()) != 0x00)
		{
			_saveSlot++;
			_saveSlotRAM = _saveInfoStartRAM + (_saveInfoLength * _saveSlot);
		}

		auto _fetchCheck = *reinterpret_cast<uint32_t*>(_saveDataStartRAM + (_saveDataLength * _saveSlot) + 0x10);

		while (_fetchCheck != _autoSaveTag && _saveSlotRAM[0] != 0x00)
		{
			_saveSlot++;
			_saveSlotRAM = _saveInfoStartRAM + (_saveInfoLength * _saveSlot);
			_fetchCheck = *reinterpret_cast<uint32_t*>(_saveDataStartRAM + (_saveDataLength * _saveSlot) + 0x10);

			while (find(_usedSlots.begin(), _usedSlots.end(), _saveName) != _usedSlots.end())
			{
				_saveOffset--;

				if (_saveOffset == 0x00)
				{
					if (!ROUND_BACK)
					{
						_saveOffset = 99;
						ROUND_BACK = true;
					}

					else
						return;
				}

				_stringStream.str("");
				_stringStream << setw(2) << setfill('0') << (_saveOffset - 1);

				_saveName = "BISLPM-66675FM-" + _stringStream.str();
			}

			if (_saveSlot >= 99)
				return;
		}

		char* _magicData = (char*)malloc(0x08);
		char* _saveData = (char*)malloc(0x10FB4);

		memcpy(_magicData, YS::AREA::SaveData, 0x08);
		memcpy(_saveData, YS::AREA::SaveData + 0x0C, 0x10FB4);

		auto _calculateChecksum = [](uint32_t _startChecksum, char* _dataArray, int _dataLength)
			{
				uint32_t _checksum = _startChecksum;

				for (uint32_t i = 0; i < _dataLength; i++)
					_checksum = CHECKSUM_TABLE[(_checksum >> 24) ^ static_cast<unsigned char>(_dataArray[i])] ^ (_checksum << 8);

				return _checksum ^ 0xFFFFFFFF;
			};

		auto _magicChecksum = _calculateChecksum(0xFFFFFFFF, _magicData, 0x08);
		auto _dataChecksum = _calculateChecksum(_magicChecksum ^ 0xFFFFFFFF, _saveData, 0x10FB4);

		char* _saveInfoAddrRAM = _saveInfoStartRAM + (_saveInfoLength * _saveSlot);
		char* _saveDataAddrRAM = _saveDataStartRAM + (_saveDataLength * _saveSlot);

		memcpy(_saveInfoAddrRAM, _saveName.c_str(), 0x11);

		memcpy(_saveInfoAddrRAM + 0x40, &_unixTime, 4);
		memcpy(_saveInfoAddrRAM + 0x48, &_unixTime, 4);

		memcpy(_saveInfoAddrRAM + 0x50, &_saveDataLength, 4);

		memcpy(_saveDataAddrRAM, _saveHeader, 4);

		memcpy(_saveDataAddrRAM + 0x04, &_saveHeaderID, 4);
		memcpy(_saveDataAddrRAM + 0x08, &_dataChecksum, 4);

		memcpy(_saveDataAddrRAM + 0x0c, _saveData, 0x10FB4);

		memcpy(YS::AREA::SaveData + 0x10, &_regularSaveTag, 0x04);

		uint32_t _saveInfoAddr = _saveInfoStartFILE + _saveInfoLength * _saveSlot;
		uint32_t _saveDataAddr = _saveDataStartFILE + _saveDataLength * _saveSlot;

		ofstream _stream(_saveFileString, ios::in | ios::out | ios::binary);

		_stream.seekp(_saveInfoAddr);
		_stream.write(_saveName.c_str(), 0x11);

		_stream.seekp(_saveInfoAddr + 0x40);
		_stream.write(reinterpret_cast<const char*>(&_unixTime), 0x04);
		_stream.seekp(_saveInfoAddr + 0x48);
		_stream.write(reinterpret_cast<const char*>(&_unixTime), 0x04);

		_stream.seekp(_saveInfoAddr + 0x50);
		_stream.write(reinterpret_cast<const char*>(&_saveDataLength), 0x04);

		_stream.seekp(_saveDataAddr);
		_stream.write(_saveHeader, 0x04);

		_stream.seekp(_saveDataAddr + 0x04);
		_stream.write(reinterpret_cast<const char*>(&_saveHeaderID), 0x04);
		_stream.seekp(_saveDataAddr + 0x08);
		_stream.write(reinterpret_cast<const char*>(&_dataChecksum), 0x04);

		_stream.seekp(_saveDataAddr + 0x0C);
		_stream.write(_saveData, 0x10FB4);

		_stream.close();

		free(_saveData);
		free(_magicData);

		SAVE_INITIATE = false;
	}
}


extern "C"
{
	__declspec(dllexport) void OnInit(wchar_t* mod_path)
	{
		MOD_PATH = wstring(mod_path);

		auto _checkSteam = FindModule("steam_api64.dll");

		if (_checkSteam)
		{
			vector<uint8_t> _nopArraySave(0x05);
			uint8_t _jumpByte = 0xEB;

			char* _checkSaveFunc = SignatureScan<char*>("\x40\x55\x56\x57\x48\x81\xEC\xA0\x00\x00\x00\x48\xC7\x44\x24\x38\xFE\xFF\xFF\xFF\x48\x89\x9C\x24\xD0\x00\x00\x00\x48\x8B\x05\x00\x00\x00\x00\x48\x33\xC4\x48\x89\x84\x24\x90\x00\x00\x00\x8B\xF1\x89\x0D\x00\x00\x00\x00\x89\x15\x00\x00\x00\x00\x33\xED\x8D\x5D\x01\x48\x39\x2D\x00\x00\x00\x00\x0F\x85\x00\x00\x00\x00\xB9\x78\x01\x00\x00\xE8\x00\x00\x00\x00\x48\x89\x44\x24\x30\x48\x85\xC0\x74\x1D", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx????xx????xxxxxxxx????xx????xxxxxx????xxxxxxxxxx");

			memcpy(_checkSaveFunc + 0x189, _nopArraySave.data(), 0x05);
			memcpy(_checkSaveFunc + 0x196, _nopArraySave.data(), 0x02);

			memcpy(_checkSaveFunc + 0x1A1, &_jumpByte, 0x01);
		}

		else
		{
			vector<uint8_t> _nopArraySave(0x05);
			uint8_t _jumpByte = 0xEB;

			char* _checkSaveFunc = SignatureScan<char*>("\x40\x57\x48\x83\xEC\x50\x48\xC7\x44\x24\x30\xFE\xFF\xFF\xFF\x48\x89\x5C\x24\x70\x48\x89\x74\x24\x78\x48\x8B\x05\x00\x00\x00\x00\x48\x33\xC4\x48\x89\x44\x24\x48\x8B\xF9\x89\x0D\x00\x00\x00\x00\x89\x15\x00\x00\x00\x00\x33\xF6\x48\x39\x35\x00\x00\x00\x00\x0F\x85\x3D\x01\x00\x00\xB9\x78\x01\x00\x00\xE8\x00\x00\x00\x00\x48\x89\x44\x24\x38\x48\x85\xC0\x74\x1D\x45\x33\xC9\x44\x8B\x05", "xxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxx????xx????xxxxx????xxxxxxxxxxxx????xxxxxxxxxxxxxxxx");

			memcpy(_checkSaveFunc + 0x138, _nopArraySave.data(), 0x05);
			memcpy(_checkSaveFunc + 0x145, _nopArraySave.data(), 0x02);

			memcpy(_checkSaveFunc + 0x150, &_jumpByte, 0x01);
		}

		char* _nopArray = new char[0x10];
		fill(_nopArray, _nopArray + 0x10, 0x90);

		char* _magicClearFunc = SignatureScan<char*>("\x48\x89\x5C\x24\x18\x48\x89\x6C\x24\x20\x57\x48\x83\xEC\x40\x48\x8B\x05\x00\x00\x00\x00\x48\x89\x74\x24\x50\x48\x8B\xD8\x4C\x89\x74\x24\x58\x48\x85\xC0\x0F\x84\x00\x00\x00\x00\x0F\x29\x74\x24\x30\xF3\x0F\x10\x35\x00\x00\x00\x00\x0F\x29\x7C\x24\x20\x0F\x57\xFF\x48\x85\xDB\x75\x08", "xxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxx????xxxxxxxxx????xxxxxxxxxxxxx");
		char* _fadeReset = reinterpret_cast<char*>(dk::SOFTRESET::SoftResetThread) + 0x1ED;

		memcpy(_fadeReset, _nopArray, 0x05); // NOP resetting the Fades so that Soft Reset is actually smooth.
		memcpy(_magicClearFunc + 0x18A, _nopArray, 0x05); // NOP clearing Magic so that it doesn't fucking crash.

		char* _pictAppearFunc = SignatureScan<char*>("\x40\x53\x48\x83\xEC\x30\x48\x63\x41\x34\x48\x8B\xD9\x3B\x41\x30\x0F\x84\x00\x00\x00\x00\x48\x69\xD0\x60\x05\x00\x00\x48\x89\x7C\x24\x48", "xxxxxxxxxxxxxxxxxx????xxxxxxxxxxxx");

		uint32_t _fetchAddress = *reinterpret_cast<const uint32_t*>(_pictAppearFunc + 0x36);
		_fetchAddress -= 0x90;

		memcpy(_pictAppearFunc + 0x36, &_fetchAddress, 0x04);

		auto _fetchAdjustment = SignatureScan<char*>("\x48\x83\xEC\x28\x0F\x10\x41\x48\x4C\x8B\xC9\x4C\x8B\xD2\xF3\x0F\x10\x25\x00\x00\x00\x00\x0F\x57\xED\x0F\x11\x02\x41\x0F\x10\x00\x49\x8B\x41\x40", "xxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxx");

		memcpy(_fetchAdjustment + 0xF6, _nopArray, 0x06);
		memcpy(_fetchAdjustment + 0x101, _nopArray, 0x06);

		// Fixed Party Limits crashing the game post-fights.

		auto _fetchLimitCheck = SignatureScan<char*>("\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x30\x8B\x79\x10\x48\x8B\xF1\x0F\x29\x74\x24\x20\xF3\x0F\x10\x71\x18\x8B\x49\x08\xE8\x00\x00\x00\x00\x8B\x48\x04\xE8\x00\x00\x00\x00\x8B\x0E\x48\x8B\xD8\xE8\x00\x00\x00\x00\x0F\x28\xDE\x44\x8B\xC7\x48\x8B\xD3\x48\x8B\xC8", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxx????xxxxxx????xxxxxxxxxxxx");
		auto _fetchLimitPint = ResolveRelativeAddress<uint64_t>("\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x20\x57\x48\x83\xEC\x30\x48\x8B\xFA\x8B\xD9\xE8\x00\x00\x00\x00\x48\x8B\xE8\x0F\xB7\x08\xE8\x00\x00\x00\x00\x48\x8B\xF0\x48\x85\xC0\x0F\x84\xCF\x00\x00\x00\xB9\x28\x01\x00\x00\xE8\x00\x00\x00\x00\x33\xDB\x48\x85\xC0", "xxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxx????xxxxxxxxxxxxxxxxxx????xxxxx", 0x6A);

		vector<uint8_t> _instFixLimits
		{
			0x4C, 0x8B, 0x1D, 0x00, 0x00, 0x00, 0x00,
			0x4D, 0x85, 0xDB,
			0x75, 0x0A,
			0xEB, 0x02,
			0xEB, 0xF0,
			0x4D, 0x31, 0xDB,
			0xC3, 0x90, 0x90
		};

		auto _limitAddrCalc = _fetchLimitPint - reinterpret_cast<uint64_t>(_fetchLimitCheck - 0x0E) - 0x07;
		memcpy(_instFixLimits.data() + 0x03, &_limitAddrCalc, 0x04);

		vector<uint64_t> _fetchFunction(0x68);
		memcpy(_fetchFunction.data(), _fetchLimitCheck, 0x68);
		memcpy(_fetchLimitCheck + 0x08, _fetchFunction.data(), 0x68);

		auto _tempPtr = reinterpret_cast<uint32_t*>(_fetchLimitCheck + 0x4E);
		*_tempPtr -= 0x08;

		_tempPtr = reinterpret_cast<uint32_t*>(_fetchLimitCheck + 0x2B);
		*_tempPtr -= 0x08;
		_tempPtr = reinterpret_cast<uint32_t*>(_fetchLimitCheck + 0x33);
		*_tempPtr -= 0x08;
		_tempPtr = reinterpret_cast<uint32_t*>(_fetchLimitCheck + 0x3D);
		*_tempPtr -= 0x08;

		memcpy(_fetchLimitCheck - 0x0E, _instFixLimits.data(), 0x16);


		wchar_t _configPath[MAX_PATH];
		wcscpy(_configPath, mod_path);
		wcscat(_configPath, L"\\dll\\archiConfig.cfg");
		auto _wideStr = wstring(_configPath);

		mINI::INIFile _configFile(string(_wideStr.begin(), _wideStr.end()));
		mINI::INIStructure _configStruct;

		_configFile.read(_configStruct);

		auto _fetchButtons = _configStruct["General"]["resetCombo"];

		if (_fetchButtons.find("NONE") == string::npos)
		{
			size_t _buttonPos = 0;
			string _buttonToken;
			string _tempStr = _fetchButtons;

			while ((_buttonPos = _tempStr.find(" + ")) != string::npos)
			{
				_buttonToken = _tempStr.substr(0, _buttonPos);
				_tempStr.erase(0, _buttonPos + 3);

				transform(_buttonToken.begin(), _buttonToken.end(), _buttonToken.begin(), ::toupper);

				RESET_COMBO |= YS::HARDPAD::BUTTONS_MAP[_buttonToken];

				if (_tempStr.find(" + ") == string::npos)
					RESET_COMBO |= YS::HARDPAD::BUTTONS_MAP[_tempStr];
			}
		}

		ROOM_AMOUNT = atoi(_configStruct["General"]["saveRoomAmount"].c_str());
		SAVE_SLOT_OFFSET = atoi(_configStruct["General"]["saveSlot"].c_str());

		if (ROOM_AMOUNT == 0x00)
			ROOM_AMOUNT = 1;
	}

	__declspec(dllexport) void OnFrame()
	{
		#ifndef  LITE_BUILD
		TriggerReset();
		AutosaveLogic();
		#endif

		RegisterMagic();
		RegisterMovement();
		ShowInformation();
		ProcessDeath();
	}
}
