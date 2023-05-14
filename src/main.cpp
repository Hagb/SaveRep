//
// Created by PinkySmile on 31/10/2020
//

// #include <SokuLib.hpp>
// clang-format off

#include <string>
#include <type_traits>
// clang-format on
#include "BattleManager.hpp"
#include "BattleMode.hpp"
#include "InputManager.hpp"
#include <iostream>
// #include "Net"
#include "Hash.hpp"
#include "NetObject.hpp"
#include "Scenes.hpp"
#include "Tamper.hpp"
#include "VTables.hpp"

static int /*SokuLib::Scene*/ (SokuLib::BattleWatch::*ogBattleWatchOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::BattleClient::*ogBattleClientOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::BattleServer::*ogBattleServerOnProcess)();
static const auto spectatingSaveReplayIfAllow = (void(__thiscall *)(SokuLib::NetObject *))(0x454240);
static const auto battleSaveReplay = (void (*)())(0x43ebe0);
static const auto get00899840 = (char *(*)())(0x0043df40);
// static const auto getReplayPath
// 	= (void(__thiscall *)(SokuLib::InputManager *, char *replay_path, const char *profile1name, const char *profile2name))(0x42cb30);
// static const auto writeReplay = (void(__thiscall *)(SokuLib::InputManager *, const char *path))(0x42b2d0);

static int /*SokuLib::Scene*/ __fastcall CBattleWatch_OnProcess(SokuLib::BattleWatch *This) {
	int ret = (This->*ogBattleWatchOnProcess)();
	if (ret == SokuLib::SCENE_TITLE) {
		std::cout << "Disconnect when spectating. Save replay if allowed." << std::endl;
		spectatingSaveReplayIfAllow(&SokuLib::getNetObject());
	}
	return ret;
}

static void battleSaveReplayIfAllow() {
	std::cout << "Save replay if allowed." << std::endl;
	switch (get00899840()[0x73]) {
	case 0: // always save replay
	case 1: // save replay when as player
		battleSaveReplay();
	case 2: // save replay when as spectator
	case 3: // never save replay
	case 4: // always ask
		break;
	}
}

template<typename T, int (T::**ogBattlePlayOnProcess)()> static int /*SokuLib::Scene*/ __fastcall CBattlePlay_OnProcess(T *This) {
	int ret = (This->**ogBattlePlayOnProcess)();
	if (ret == SokuLib::SCENE_TITLE) {
		std::cout << "Disconnect. ";
		battleSaveReplayIfAllow();
	}
	return ret;
}

template<SokuLib::Scene retcode> static void __declspec(naked) gameEndTooEarly() {
	static const SokuLib::Scene retcode_ = retcode; // workaround for the template parameter is unusable in inline asm (why?)
	std::cout << "Esc or desync causes the game ends too early. ";
	battleSaveReplayIfAllow();
	__asm {
		pop edi;
		mov eax, retcode_;
		pop esi;
		ret;
	}
}

static void __declspec(naked) gameEndTooEarly2() {
	static auto gameEndTooEarlyAddr = gameEndTooEarly<SokuLib::SCENE_SELECTSV>;
	static const void *fun004282d0 = (void *)0x004282d0;
	__asm {
		call fun004282d0;
		jmp gameEndTooEarlyAddr;
	}
}
static void __declspec(naked) gameEndTooEarly3() {
	static const void *addr004283a2 = (void *)0x004283a2;
	__asm {
		push esi;
	}
	std::cout << "Esc or desync causes the game ends too early. ";
	battleSaveReplayIfAllow();
	__asm {
		pop esi;
		cmp [esi+0x6c8], 0;
		jmp addr004283a2;
	}
}

// We check if the game version is what we target (in our case, Soku 1.10a).
extern "C" __declspec(dllexport) bool CheckVersion(const BYTE hash[16]) {
	return memcmp(hash, SokuLib::targetHash, sizeof(SokuLib::targetHash)) == 0;
}

// Called when the mod loader is ready to initialize this module.
// All hooks should be placed here. It's also a good moment to load settings
// from the ini.
extern "C" __declspec(dllexport) bool Initialize(HMODULE hMyModule, HMODULE hParentModule) {
	DWORD old;

#ifdef _DEBUG
	FILE *_;

	AllocConsole();
	freopen_s(&_, "CONOUT$", "w", stdout);
	freopen_s(&_, "CONOUT$", "w", stderr);
#endif
	VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
	ogBattleWatchOnProcess = SokuLib::TamperDword(&SokuLib::VTable_BattleWatch.onProcess, CBattleWatch_OnProcess);
	ogBattleServerOnProcess
		= SokuLib::TamperDword(&SokuLib::VTable_BattleServer.onProcess, CBattlePlay_OnProcess<SokuLib::BattleServer, &ogBattleServerOnProcess>);
	ogBattleClientOnProcess
		= SokuLib::TamperDword(&SokuLib::VTable_BattleClient.onProcess, CBattlePlay_OnProcess<SokuLib::BattleClient, &ogBattleClientOnProcess>);
	VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, old, &old);
	VirtualProtect((PVOID)TEXT_SECTION_OFFSET, TEXT_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
	SokuLib::TamperNearJmp(0x428663, gameEndTooEarly<SokuLib::SCENE_SELECTCL>);
	SokuLib::TamperNearJmp(0x428680, gameEndTooEarly<SokuLib::SCENE_SELECTCL>);
	SokuLib::TamperNearJmp(0x4283b0, gameEndTooEarly<SokuLib::SCENE_SELECTSV>);
	SokuLib::TamperNearJmp(0x42838e, gameEndTooEarly2);
	VirtualProtect((PVOID)TEXT_SECTION_OFFSET, TEXT_SECTION_SIZE, old, &old);

	FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
	return true;
}

extern "C" int APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved) {
	return TRUE;
}

// New mod loader functions
// Loading priority. Mods are loaded in order by ascending level of priority
// (the highest first). When 2 mods define the same loading priority the loading
// order is undefined.
extern "C" __declspec(dllexport) int getPriority() {
	return 0;
}

// Not yet implemented in the mod loader, subject to change
// SokuModLoader::IValue **getConfig();
// void freeConfig(SokuModLoader::IValue **v);
// bool commitConfig(SokuModLoader::IValue *);
// const char *getFailureReason();
// bool hasChainedHooks();
// void unHook();