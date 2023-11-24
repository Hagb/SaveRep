// Copyright (c) 2023 Junyu Guo (Hagb)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The template was created by PinkySmile on 31/10/2020

// #include <SokuLib.hpp>
// clang-format off
#include <type_traits>
// clang-format on
#include "BattleManager.hpp"
#include "BattleMode.hpp"
#include "Hash.hpp"
#include "InputManager.hpp"
#include "NetObject.hpp"
#include "Scenes.hpp"
#include "Tamper.hpp"
#include "TextureManager.hpp"
#include "VTables.hpp"
#include <WinUser.h>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <mutex>
#include <ostream>
#include <processthreadsapi.h>
#include <thread>

using namespace std::chrono_literals;
static int /*SokuLib::Scene*/ (SokuLib::Battle::*ogBattleOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::BattleWatch::*ogBattleWatchOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::BattleClient::*ogBattleClientOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::BattleServer::*ogBattleServerOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::Title::*ogTitleOnProcess)();
static LPTOP_LEVEL_EXCEPTION_FILTER ogExceptionFilter;
static const auto spectatingSaveReplayIfAllow = (void(__thiscall *)(SokuLib::NetObject *))(0x454240);
static const auto battleSaveReplay = (void (*)())(0x43ebe0);
static const auto get00899840 = (char *(*)())(0x0043df40);
// static const auto getReplayPath
// 	= (void(__thiscall *)(SokuLib::InputManager *, char *replay_path, const char *profile1name, const char *profile2name))(0x42cb30);
// static const auto writeReplay = (void(__thiscall *)(SokuLib::InputManager *, const char *path))(0x42b2d0);
static WNDPROC ogWndProc;
static bool isBeingClosed = false;
static DWORD crashThreadId = 0;
static bool isCrashedHandled = false;
static bool hasUrgentlySaved = false;
static std::mutex urgentlySaveFinishMtx;
static std::condition_variable urgentlySaveFinishCV;
static std::timed_mutex saveLock;
static std::mutex crashLock;
static SokuLib::Scene lastSceneInBattle = SokuLib::SCENE_TITLE;
// #define CRASH_TEST
#ifdef CRASH_TEST
static char lastKey = 0;
void crash() {
	std::cout << "manually crash" << std::endl;
	__asm {
        mov eax, ds:0;
	}
}
#endif

static void spectatingSaveReplay_() {
	std::cout << "Save replay." << std::endl;
	auto old_option = get00899840()[0x73];
	// switch (get00899840()[0x73]) {
	// case 0: // always save replay
	// case 1: // save replay when as player
	// case 2: // save replay when as spectator
	// case 3: // never save replay
	// case 4: // always ask
	// }
	get00899840()[0x73] = 0;
	spectatingSaveReplayIfAllow(&SokuLib::getNetObject());
	get00899840()[0x73] = old_option;
}

static void battleSaveReplay_() {
	std::cout << "Save replay." << std::endl;
	battleSaveReplay();
}

static void (*getCurrentSaveFunction())() {
	switch (lastSceneInBattle) {
	case SokuLib::Scene::SCENE_BATTLECL:
	case SokuLib::Scene::SCENE_BATTLESV:
		return battleSaveReplay_;
	case SokuLib::Scene::SCENE_BATTLE:
		if (SokuLib::subMode != SokuLib::BATTLE_SUBMODE_PLAYING2)
			return nullptr;
		switch (SokuLib::mainMode) {
		case SokuLib::BATTLE_MODE_STORY:
		case SokuLib::BATTLE_MODE_VSCOM:
		case SokuLib::BATTLE_MODE_VSPLAYER:
		case SokuLib::BATTLE_MODE_TIME_TRIAL:
			return battleSaveReplay_;
		default:
			return nullptr;
		}
	case SokuLib::Scene::SCENE_BATTLEWATCH:
		return spectatingSaveReplay_;
	default:
		return nullptr;
	}
}

template<typename T, int (T::**ogBattleOnProcess)()> static int /*SokuLib::Scene*/ __fastcall CBattles_OnProcess(T *This) {
#ifdef CRASH_TEST
	if (lastKey == 'k')
		crash();
	if (lastKey == 't')
		Sleep(500);
#endif
	std::unique_lock<std::timed_mutex> saveLock_(saveLock);
#ifdef CRASH_TEST
	if (lastKey == 't')
		Sleep(500000);
#endif
	int ret = (This->**ogBattleOnProcess)();
	if (!hasUrgentlySaved) {
		auto save = getCurrentSaveFunction();
		if (save) {
			if (isBeingClosed) {
				std::cout << "Window is being closed. ";
				save();
				hasUrgentlySaved = true;
				std::unique_lock<std::mutex> saveFinishMtx_(urgentlySaveFinishMtx);
				urgentlySaveFinishCV.notify_all();
			} else if (ret == SokuLib::SCENE_TITLE && lastSceneInBattle != SokuLib::SCENE_BATTLE) {
				std::cout << "Disconnect. ";
				save();
			}
		}
	}
	lastSceneInBattle = (SokuLib::Scene)ret;
	return ret;
}

template<SokuLib::Scene retcode> static void __declspec(naked) gameEndTooEarly() {
	static const SokuLib::Scene retcode_ = retcode; // workaround for the template parameter is unusable in inline asm (why?)
	std::cout << "Esc or desync causes the game ends too early. ";
	battleSaveReplay_();
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
	battleSaveReplay_();
	__asm {
		pop esi;
		cmp [esi+0x6c8], 0;
		jmp addr004283a2;
	}
}

static LRESULT __stdcall hookedWndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
#ifdef CRASH_TEST
	if (uMsg == WM_CHAR) {
		switch (wParam) {
		case 'j':
			crash();
			break;
		case 't':
			lastKey = wParam;
			Sleep(100);
			crash();
			break;
		default:
			lastKey = wParam;
		}
	}
#endif
	if (isBeingClosed || !(uMsg == WM_DESTROY || uMsg == WM_CLOSE))
		return CallWindowProc(ogWndProc, hWnd, uMsg, wParam, lParam);
	{
		if (getCurrentSaveFunction()) {
			std::cout << "Window is being closed. Try to urgently save rep." << std::endl;
			isBeingClosed = true;
			std::unique_lock<std::mutex> saveFinishMtx_(urgentlySaveFinishMtx);
			urgentlySaveFinishCV.wait(saveFinishMtx_, [] { return hasUrgentlySaved; });
		}
	}
	return CallWindowProc(ogWndProc, hWnd, uMsg, wParam, lParam);
}

template<class _Lock> class TimedMutex_ {
public:
	void lock() {
		_lock.try_lock_for(100ms);
	}
	void unlock() {
		_lock.unlock();
	}
	TimedMutex_(_Lock &_lock): _lock(_lock) {}

private:
	_Lock &_lock;
};

static void saveInCrash() {
	if (crashThreadId == 0 || isCrashedHandled)
		return;
	isCrashedHandled = true;
	if (!getCurrentSaveFunction())
		return;

	FILE *_;

	AllocConsole();
	freopen_s(&_, "CONOUT$", "w", stdout);
	freopen_s(&_, "CONOUT$", "w", stderr);

	std::cout << "The game has crashed! Try to save replay." << std::endl;
	if (GetThreadId(*(HANDLE *)(0x89ff90 + 0x50)) != crashThreadId) {
		std::cout << "Try to urgently (and gracefully) save replay." << std::endl;
		isBeingClosed = true;
		std::unique_lock<std::mutex> saveFinishMtx_(urgentlySaveFinishMtx);
		if (urgentlySaveFinishCV.wait_for(saveFinishMtx_, 2000ms, [] { return hasUrgentlySaved; })) {
			std::cout << "Replay has been saved urgently and gracefully" << std::endl;
			MessageBoxA(NULL, "Replay has been saved urgently and gracefully", "Replay has been saved", 0);
			return;
		} else
			std::cout << "Cannot not save replay gracefully (timeout)!" << std::endl;
	} else
		std::cout << "The thread saving replay in original game has crashed!" << std::endl;

	std::cout << "Try to save replay forcely." << std::endl;
	std::unique_lock<std::timed_mutex> saveLock_(saveLock, std::defer_lock);
	if (!saveLock_.try_lock_for(1000ms))
		std::cout << "Failed to gain lock (timeout). Ignore it." << std::endl;
	std::thread thread_([] {
		__try {
			auto save = getCurrentSaveFunction();
			if (save) {
				save();
				std::cout << "Replay has been saved urgently and forcely" << std::endl;
				MessageBoxA(NULL, "Replay has been saved urgently and forcely", "Replay has been saved", 0);
				return;
			} else
				std::cout << "Unexpected status: scene=" << (int)lastSceneInBattle << ". Give up." << std::endl;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "Exception when saving replay forcely." << std::endl;
		}
		MessageBoxA(NULL, "Failed to save replay urgently", "Cannot save replay", MB_ICONERROR);
	});
	thread_.join();
}

static LONG WINAPI exceptionFilter(PEXCEPTION_POINTERS ExPtr) {
	std::cout << "SaveRep exception filter is triggered." << std::endl;
	std::unique_lock<std::mutex> crashLock_(crashLock);
	if (!crashThreadId) {
		crashThreadId = GetCurrentThreadId();
		saveInCrash();
	}
	crashLock_.unlock();
	if (ogExceptionFilter) {
		std::cout << "Call original exception filter." << std::endl;
		return ogExceptionFilter(ExPtr);
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

static int /*SokuLib::Scene*/ __fastcall myTitleOnProcess(SokuLib::Title *This) {
	int ret = (This->*ogTitleOnProcess)();
	static bool hasSetFilter = false;
	if (ogWndProc) {
		if (hasSetFilter)
			return ret;
		// when this function is called second time
		std::unique_lock<std::mutex> crashLock_(crashLock);
		ogExceptionFilter = SetUnhandledExceptionFilter(exceptionFilter);
		hasSetFilter = true;
		return ret;
	}
	ogWndProc = (WNDPROC)SetWindowLongPtr(SokuLib::window, GWL_WNDPROC, (LONG_PTR)hookedWndProc);
	if (!ogWndProc) {
		std::cout << "SetWindowLongPtr Error: " << GetLastError() << std::endl;
		ogWndProc = (WNDPROC)0xffffffff;
	}
	return ret;
}

// We check if the game version is what we target (in our case, Soku 1.10a).
extern "C" __declspec(dllexport) bool CheckVersion(const BYTE hash[16]) {
	return memcmp(hash, SokuLib::targetHash, sizeof(SokuLib::targetHash)) == 0;
}

// Called when the mod loader is ready to initialize this module.
// All hooks should be placed here. It's also a good moment to load settings
// from the ini.
extern "C" __declspec(dllexport) bool Initialize(HMODULE hMyModule, HMODULE hParentModule) {
#ifdef _DEBUG
	FILE *_;

	AllocConsole();
	freopen_s(&_, "CONOUT$", "w", stdout);
	freopen_s(&_, "CONOUT$", "w", stderr);
#endif
	DWORD old;
	VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
	ogBattleOnProcess = SokuLib::TamperDword(&SokuLib::VTable_Battle.onProcess, CBattles_OnProcess<SokuLib::Battle, &ogBattleOnProcess>);
	ogBattleWatchOnProcess = SokuLib::TamperDword(&SokuLib::VTable_BattleWatch.onProcess, CBattles_OnProcess<SokuLib::BattleWatch, &ogBattleWatchOnProcess>);
	ogBattleServerOnProcess = SokuLib::TamperDword(&SokuLib::VTable_BattleServer.onProcess, CBattles_OnProcess<SokuLib::BattleServer, &ogBattleServerOnProcess>);
	ogBattleClientOnProcess = SokuLib::TamperDword(&SokuLib::VTable_BattleClient.onProcess, CBattles_OnProcess<SokuLib::BattleClient, &ogBattleClientOnProcess>);
	ogTitleOnProcess = SokuLib::TamperDword(&SokuLib::VTable_Title.onProcess, myTitleOnProcess);
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