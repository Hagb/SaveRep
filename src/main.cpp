// Copyright (c) 2024 Junyu Guo (Hagb)
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
#include "resource.h"
#include <WinUser.h>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <mutex>
#include <ostream>
#include <processthreadsapi.h>
#include <string>
#include <thread>

using namespace std::chrono_literals;
HINSTANCE hModule;
static int /*SokuLib::Scene*/ (SokuLib::Battle::*ogBattleOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::BattleWatch::*ogBattleWatchOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::BattleClient::*ogBattleClientOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::BattleServer::*ogBattleServerOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::SelectClient::*ogSelectClientOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::SelectServer::*ogSelectServerOnProcess)();
static int /*SokuLib::Scene*/ (SokuLib::Title::*ogTitleOnProcess)();
static LPTOP_LEVEL_EXCEPTION_FILTER ogExceptionFilter;
static const auto spectatingSaveReplayIfAllow = (void(__thiscall *)(SokuLib::NetObject *))(0x454240);
static const auto battleSaveReplay = (void (*)())(0x43ebe0);
static const auto get00899840 = (char *(*)())(0x0043df40);
static const auto isGamePaused = (*(bool (*)())0x43e740);
// static const auto getReplayPath
// 	= (void(__thiscall *)(SokuLib::InputManager *, char *replay_path, const char *profile1name, const char *profile2name))(0x42cb30);
// static const auto writeReplay = (void(__thiscall *)(SokuLib::InputManager *, const char *path))(0x42b2d0);
static WNDPROC ogWndProc;
static bool isBeingClosed = false;
static unsigned int crashCount = 0;
static bool hasUrgentlySaved = false;
static std::mutex urgentlySaveFinishMtx;
static std::condition_variable urgentlySaveFinishCV;
static std::timed_mutex saveLock;
static std::timed_mutex crashLock;
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

static void SpectatingSaveReplay_() {
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

static void BattleSaveReplay_() {
	std::cout << "Save replay." << std::endl;
	battleSaveReplay();
}

static void (*GetCurrentSaveFunction())() {
	switch (lastSceneInBattle) {
	case SokuLib::Scene::SCENE_BATTLECL:
	case SokuLib::Scene::SCENE_BATTLESV:
		return BattleSaveReplay_;
	case SokuLib::Scene::SCENE_BATTLE:
		if (SokuLib::subMode != SokuLib::BATTLE_SUBMODE_PLAYING2)
			return nullptr;
		switch (SokuLib::mainMode) {
		case SokuLib::BATTLE_MODE_STORY:
		case SokuLib::BATTLE_MODE_VSCOM:
		case SokuLib::BATTLE_MODE_VSPLAYER:
		case SokuLib::BATTLE_MODE_TIME_TRIAL:
			return BattleSaveReplay_;
		default:
			return nullptr;
		}
	case SokuLib::Scene::SCENE_BATTLEWATCH:
		return SpectatingSaveReplay_;
	default:
		return nullptr;
	}
}

void SaveRepIfNeeded(SokuLib::Scene newSceneId_) {
	if (!hasUrgentlySaved) {
		auto save = GetCurrentSaveFunction();

		if (save) {
			if (isBeingClosed) {
				std::cout << "Window is being closed. ";
				save();
				hasUrgentlySaved = true;
				std::unique_lock<std::mutex> saveFinishMtx_(urgentlySaveFinishMtx);
				urgentlySaveFinishCV.notify_all();
			} else if (newSceneId_ == SokuLib::SCENE_TITLE && lastSceneInBattle != SokuLib::SCENE_BATTLE) {
				std::cout << "Disconnect. ";
				save();
			} else if (newSceneId_ == SokuLib::SCENE_SELECTCL || newSceneId_ == SokuLib::SCENE_SELECTSV) {
				SokuLib::NetObject *netobject = &SokuLib::getNetObject();
				int unknown = (**(int(__fastcall **)(SokuLib::NetObject *))(*(unsigned int *)netobject + 0x30))(netobject);
				bool paused = isGamePaused();
				int battleStatus = ((int *)&SokuLib::getBattleMgr())[0x22];
				if (!((unknown == 5 || unknown == 3) && paused && battleStatus == 7)) { // unknown == 3, paused == 1, battleStatus == 7 : giuroll
					std::string reason;
					if (unknown == 3 && !paused && battleStatus != 4)
						reason = "your opponent pressed Esc or desync happened";
					else if (unknown == 5 && !paused && battleStatus == 4)
						reason = "your opponent pressed Esc"; // giuroll
					else if (unknown == 5 && !paused && battleStatus != 7)
						reason = "you pressed Esc";
					else if (unknown == 3 && !paused && battleStatus == 4)
						reason = "desync happened and the client of your opponent thought it won"; // giuroll
					if (reason.empty()) {
						std::cout << "Battle is stopped because of unknown reason";
					} else
						std::cout << "Battle is stopped probably because " + reason;
					std::cout << " (" << unknown << ", ";
					std::cout << paused << ", ";
					std::cout << battleStatus << "). ";
					save();
				}
#ifdef _DEBUG
				else {
					std::cout << "Battle ends with (" << unknown << ", ";
					std::cout << paused << ", ";
					std::cout << battleStatus << "). " << std::endl;
				}
#endif
			}
		}
	}
	lastSceneInBattle = (SokuLib::Scene)newSceneId_;
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
	SaveRepIfNeeded((SokuLib::Scene)ret);
	return ret;
}

template<typename T, int (T::**ogSelectOnProcess)()> static int /*SokuLib::Scene*/ __fastcall CSelects_OnProcess(T *This) {
	{
		std::unique_lock<std::timed_mutex> saveLock_(saveLock);
		SaveRepIfNeeded(SokuLib::newSceneId);
	}
	return (This->**ogSelectOnProcess)();
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
		if (GetCurrentSaveFunction()) {
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

static void SaveInCrash(DWORD crashedThreadId) {
	if (!GetCurrentSaveFunction())
		return;

	FILE *_;

	AllocConsole();
	std::cout.flush();
	freopen_s(&_, "CONOUT$", "w", stdout);
	freopen_s(&_, "CONOUT$", "w", stderr);
	std::cout.clear();

	std::cout << "The game has crashed! Try to save replay." << std::endl;
	if (GetThreadId(*(HANDLE *)(0x89ff90 + 0x50)) != crashedThreadId) {
		std::cout << "The game loop thread seems still alive. Try to urgently (and gracefully) save replay." << std::endl;
		isBeingClosed = true;
		std::unique_lock<std::mutex> saveFinishMtx_(urgentlySaveFinishMtx);
		if (urgentlySaveFinishCV.wait_for(saveFinishMtx_, 2000ms, [] { return hasUrgentlySaved; })) {
			std::cout << "Replay has been saved urgently and gracefully." << std::endl;
			const wchar_t *text, *title;
			size_t text_len = LoadStringW(hModule, IDS_CRASH_REPLAY_SAVE_GRACEFULLY, (wchar_t *)&text, 0);
			size_t title_len = LoadStringW(hModule, IDS_Replay_has_been_saved, (wchar_t *)&title, 0);
			text = (wchar_t *)memcpy(calloc(text_len + 1, sizeof(wchar_t)), text, sizeof(wchar_t) * text_len);
			title = (wchar_t *)memcpy(calloc(title_len + 1, sizeof(wchar_t)), title, sizeof(wchar_t) * title_len);
			MessageBoxW(NULL, text, title, 0);
			free((void *)text);
			free((void *)title);
			return;
		} else
			std::cout << "Cannot not save replay gracefully (timeout)!" << std::endl;
	} else
		std::cout << "The game loop thread has crashed!" << std::endl;

	std::cout << "Try to save replay forcely." << std::endl;
	std::unique_lock<std::timed_mutex> saveLock_(saveLock, std::defer_lock);
	if (!saveLock_.try_lock_for(1000ms))
		std::cout << "Failed to gain lock (timeout). Ignore it." << std::endl;
	std::thread thread_([] {
		__try {
			auto save = GetCurrentSaveFunction();
			if (save) {
				save();
				std::cout << "Replay has been saved urgently and forcely." << std::endl;
				const wchar_t *text, *title;
				size_t text_len = LoadStringW(hModule, IDS_CRASH_REPLAY_SAVE_FORCELY, (wchar_t *)&text, 0);
				size_t title_len = LoadStringW(hModule, IDS_Replay_has_been_saved, (wchar_t *)&title, 0);
				text = (wchar_t *)memcpy(calloc(text_len + 1, sizeof(wchar_t)), text, sizeof(wchar_t) * text_len);
				title = (wchar_t *)memcpy(calloc(title_len + 1, sizeof(wchar_t)), title, sizeof(wchar_t) * title_len);
				MessageBoxW(NULL, text, title, 0);
				free((void *)text);
				free((void *)title);
				return;
			} else
				std::cout << "Unexpected status: scene=" << (int)lastSceneInBattle << ". Give up." << std::endl;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "The thread threw an exception when saving replay forcely." << std::endl;
		}
		const wchar_t *text, *title;
		size_t text_len = LoadStringW(hModule, IDS_CRASH_REPLAY_SAVE_FAILED, (wchar_t *)&text, 0);
		size_t title_len = LoadStringW(hModule, IDS_Cannot_save_replay, (wchar_t *)&title, 0);
		text = (wchar_t *)memcpy(calloc(text_len + 1, sizeof(wchar_t)), text, sizeof(wchar_t) * text_len);
		title = (wchar_t *)memcpy(calloc(title_len + 1, sizeof(wchar_t)), title, sizeof(wchar_t) * title_len);
		MessageBoxW(NULL, text, title, MB_ICONERROR);
		free((void *)text);
		free((void *)title);
	});
	thread_.join();
}

static LONG WINAPI exceptionFilter(PEXCEPTION_POINTERS ExPtr) {
	std::string threadInfo = "Thread " + std::to_string(GetCurrentThreadId()) + ": ";
	std::cout << threadInfo << "SaveRep exception filter is triggered." << std::endl;
	std::unique_lock<std::timed_mutex> crashLock_(crashLock, std::defer_lock);
	while (!crashLock_.try_lock_for(1s)) {
		std::cout << threadInfo << "Waiting for the older crash handler..." << std::endl;
	}
	if (crashCount++ == 0) {
		std::cout << threadInfo << "Try to save replay." << std::endl;
		DWORD crashedThreadId = GetCurrentThreadId();
		std::thread thread([crashedThreadId] {
			__try {
				SaveInCrash(crashedThreadId);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				std::cout << "The thread to save replay threw an exception." << std::endl;
			}
		});
		thread.join();
	} else
		std::cout << threadInfo << "Crash " << crashCount << " .No need to save rep, since we have tried to save it." << std::endl;
	crashLock_.unlock();
	if (ogExceptionFilter) {
		std::cout << threadInfo << "Call original exception filter." << std::endl;
		LONG ret = ogExceptionFilter(ExPtr);
		std::cout << threadInfo << "original exception filter finished." << std::endl;
		return ret;
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
		std::unique_lock<std::timed_mutex> crashLock_(crashLock);
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
	std::cout.flush();
	freopen_s(&_, "CONOUT$", "w", stdout);
	freopen_s(&_, "CONOUT$", "w", stderr);
	std::cout.clear();
#endif
	DWORD old;
	VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
	ogBattleOnProcess = SokuLib::TamperDword(&SokuLib::VTable_Battle.onProcess, CBattles_OnProcess<SokuLib::Battle, &ogBattleOnProcess>);
	ogBattleWatchOnProcess = SokuLib::TamperDword(&SokuLib::VTable_BattleWatch.onProcess, CBattles_OnProcess<SokuLib::BattleWatch, &ogBattleWatchOnProcess>);
	ogBattleServerOnProcess = SokuLib::TamperDword(&SokuLib::VTable_BattleServer.onProcess, CBattles_OnProcess<SokuLib::BattleServer, &ogBattleServerOnProcess>);
	ogBattleClientOnProcess = SokuLib::TamperDword(&SokuLib::VTable_BattleClient.onProcess, CBattles_OnProcess<SokuLib::BattleClient, &ogBattleClientOnProcess>);
	ogSelectServerOnProcess = SokuLib::TamperDword(&SokuLib::VTable_SelectServer.onProcess, CSelects_OnProcess<SokuLib::SelectServer, &ogSelectServerOnProcess>);
	ogSelectClientOnProcess = SokuLib::TamperDword(&SokuLib::VTable_SelectClient.onProcess, CSelects_OnProcess<SokuLib::SelectClient, &ogSelectClientOnProcess>);
	ogTitleOnProcess = SokuLib::TamperDword(&SokuLib::VTable_Title.onProcess, myTitleOnProcess);
	VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, old, &old);
	FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
	return true;
}

extern "C" int APIENTRY DllMain(HMODULE hModule_, DWORD fdwReason, LPVOID lpReserved) {
	if (fdwReason == DLL_PROCESS_ATTACH)
		hModule = hModule_;
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
