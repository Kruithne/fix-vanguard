#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shlobj.h>
#include <conio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define APP_ID L"1985820"
#define GAME_FOLDER L"steamapps\\common\\Call of Duty Vanguard"
#define STEAM_DLL L"steam_api64.dll"
#define STEAM_KEY L"Software\\Valve\\Steam"
#define STEAM_PATH_VALUE L"SteamPath"
#define LIBRARY_FILE L"steamapps\\libraryfolders.vdf"
#define LIBRARY_PATH_TOKEN "\"path\""
#define SETTINGS_FOLDER L"Call of Duty Vanguard\\players"
#define SETTINGS_PATTERN L"settings.*.Vanguard"

#define PROGRESSION_DATA_OFFSET 19
#define COUNTER_SIZE 4
#define MAX_FILE_SIZE (16 * 1024 * 1024)
#define PATH_SIZE 1024
#define PRINT_SIZE 2048

#define STATS_TIMEOUT_MS 10000
#define STORE_WAIT_MS 3000
#define PUMP_INTERVAL_MS 100

typedef struct {
	const char *id;
	const wchar_t *title;
	int bit_offset;
	int target;
} Achievement;

typedef struct {
	HMODULE module;
	bool (*init)(void);
	void (*shutdown)(void);
	void (*run_callbacks)(void);
	void *(*user_stats)(void);
	bool (*request_current_stats)(void *stats);
	bool (*get_achievement)(void *stats, const char *name, bool *achieved);
	bool (*set_achievement)(void *stats, const char *name);
	bool (*store_stats)(void *stats);
} Steam;

static const uint8_t PROGRESSION_SIGNATURE[] = { 0x9A, 0xF7, 0x18, 0x9F, 0x1D, 0x57, 0x7F, 0xA9, 0x1D, 0x00 };

static const Achievement ACHIEVEMENTS[] = {
	{ "SP_DID_I_GET_EM", L"Did I Get 'Em?", 0xEE0, 15 },
	{ "SP_MUNITION_MAGPIE", L"Munition Magpie", 0xD38, 15 },
	{ "SP_GOTTA_TRY_THEM_ALL", L"Gotta Try Them All!", 0xE18, 15 },
	{ "SP_OH_YEAH", L"OH YEAH!", 0xD18, 5 },
	{ "SP_NOT_YOUR_FAIRY_GODMOTHER", L"Not Your Fairy Godmother", 0xEA0, 15 },
	{ "SP_SHOT_CALLER", L"Shot Caller", 0xF20, 15 },
	{ "SP_POP_GOES_THE_NIGHTINGALE", L"Pop Goes the Nightingale", 0xD98, 15 },
	{ "SP_HEIGHTENED_SENSES", L"Heightened Senses", 0xD58, 15 },
	{ "SP_POCKET_FULL_OF_FUN", L"Pocket Full of Fun", 0xCF8, 4 }
};

#define ACHIEVEMENT_COUNT ((int)(sizeof(ACHIEVEMENTS) / sizeof(ACHIEVEMENTS[0])))

static void print(const wchar_t *format, ...)
{
	wchar_t text[PRINT_SIZE];
	va_list args;

	va_start(args, format);
	int length = vswprintf(text, PRINT_SIZE, format, args);
	va_end(args);

	if (length < 0)
		return;

	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD written;

	if (WriteConsoleW(output, text, (DWORD)length, &written, NULL))
		return;

	char bytes[PRINT_SIZE * 3];
	int size = WideCharToMultiByte(CP_UTF8, 0, text, length, bytes, sizeof(bytes), NULL, NULL);
	WriteFile(output, bytes, (DWORD)size, &written, NULL);
}

static bool file_exists(const wchar_t *path)
{
	DWORD attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static uint8_t *read_file(const wchar_t *path, DWORD *size)
{
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return NULL;

	DWORD length = GetFileSize(file, NULL);
	uint8_t *data = NULL;

	if (length != INVALID_FILE_SIZE && length <= MAX_FILE_SIZE)
		data = malloc(length + 1);

	DWORD read = 0;
	if (data && (!ReadFile(file, data, length, &read, NULL) || read != length)) {
		free(data);
		data = NULL;
	}

	CloseHandle(file);

	if (!data)
		return NULL;

	data[length] = 0;
	*size = length;
	return data;
}

static bool find_settings_file(wchar_t *path)
{
	wchar_t documents[MAX_PATH];
	if (FAILED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, documents)))
		return false;

	wchar_t pattern[PATH_SIZE];
	swprintf(pattern, PATH_SIZE, L"%ls\\%ls\\%ls", documents, SETTINGS_FOLDER, SETTINGS_PATTERN);

	WIN32_FIND_DATAW entry;
	HANDLE search = FindFirstFileW(pattern, &entry);
	if (search == INVALID_HANDLE_VALUE)
		return false;

	FILETIME newest = { 0, 0 };
	bool found = false;

	do {
		if (found && CompareFileTime(&entry.ftLastWriteTime, &newest) <= 0)
			continue;

		newest = entry.ftLastWriteTime;
		found = true;
		swprintf(path, PATH_SIZE, L"%ls\\%ls\\%ls", documents, SETTINGS_FOLDER, entry.cFileName);
	} while (FindNextFileW(search, &entry));

	FindClose(search);
	return found;
}

static bool read_counters(const uint8_t *data, DWORD size, int *counters)
{
	DWORD signature_size = sizeof(PROGRESSION_SIGNATURE);

	for (DWORD pos = 0; pos + signature_size <= size; pos++) {
		if (memcmp(data + pos, PROGRESSION_SIGNATURE, signature_size) != 0)
			continue;

		DWORD base = pos + PROGRESSION_DATA_OFFSET;

		for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
			DWORD offset = base + ACHIEVEMENTS[i].bit_offset / 8;
			if (offset + COUNTER_SIZE > size)
				return false;

			int32_t value;
			memcpy(&value, data + offset, COUNTER_SIZE);
			counters[i] = value;
		}

		return true;
	}

	return false;
}

static bool dll_in_folder(const wchar_t *folder, wchar_t *dll_path)
{
	swprintf(dll_path, PATH_SIZE, L"%ls\\%ls", folder, STEAM_DLL);
	return file_exists(dll_path);
}

static bool dll_in_steam_libraries(wchar_t *dll_path)
{
	wchar_t steam_path[MAX_PATH];
	DWORD steam_path_size = sizeof(steam_path);

	if (RegGetValueW(HKEY_CURRENT_USER, STEAM_KEY, STEAM_PATH_VALUE, RRF_RT_REG_SZ, NULL, steam_path, &steam_path_size) != ERROR_SUCCESS)
		return false;

	wchar_t folder[PATH_SIZE];
	swprintf(folder, PATH_SIZE, L"%ls\\%ls", steam_path, GAME_FOLDER);
	if (dll_in_folder(folder, dll_path))
		return true;

	wchar_t library_file[PATH_SIZE];
	swprintf(library_file, PATH_SIZE, L"%ls\\%ls", steam_path, LIBRARY_FILE);

	DWORD size;
	char *text = (char *)read_file(library_file, &size);
	if (!text)
		return false;

	bool found = false;
	char *cursor = text;

	while (!found && (cursor = strstr(cursor, LIBRARY_PATH_TOKEN))) {
		cursor += strlen(LIBRARY_PATH_TOKEN);

		char *start = strchr(cursor, '"');
		if (!start)
			break;

		char library[MAX_PATH];
		int length = 0;

		for (cursor = start + 1; *cursor && *cursor != '"' && length < MAX_PATH - 1; cursor++) {
			if (*cursor == '\\' && cursor[1] == '\\')
				cursor++;

			library[length++] = *cursor;
		}

		library[length] = 0;

		wchar_t library_wide[MAX_PATH];
		if (!MultiByteToWideChar(CP_UTF8, 0, library, -1, library_wide, MAX_PATH))
			continue;

		swprintf(folder, PATH_SIZE, L"%ls\\%ls", library_wide, GAME_FOLDER);
		found = dll_in_folder(folder, dll_path);
	}

	free(text);
	return found;
}

static bool find_steam_dll(const wchar_t *game_folder, wchar_t *dll_path)
{
	if (game_folder)
		return dll_in_folder(game_folder, dll_path);

	wchar_t folder[PATH_SIZE];
	DWORD length = GetModuleFileNameW(NULL, folder, PATH_SIZE);
	wchar_t *slash = length > 0 && length < PATH_SIZE ? wcsrchr(folder, L'\\') : NULL;

	if (slash) {
		*slash = 0;
		if (dll_in_folder(folder, dll_path))
			return true;
	}

	return dll_in_steam_libraries(dll_path);
}

static bool load_steam(const wchar_t *dll_path, Steam *steam)
{
	steam->module = LoadLibraryW(dll_path);
	if (!steam->module)
		return false;

	steam->init = (void *)GetProcAddress(steam->module, "SteamAPI_Init");
	steam->shutdown = (void *)GetProcAddress(steam->module, "SteamAPI_Shutdown");
	steam->run_callbacks = (void *)GetProcAddress(steam->module, "SteamAPI_RunCallbacks");
	steam->user_stats = (void *)GetProcAddress(steam->module, "SteamAPI_SteamUserStats_v012");
	steam->request_current_stats = (void *)GetProcAddress(steam->module, "SteamAPI_ISteamUserStats_RequestCurrentStats");
	steam->get_achievement = (void *)GetProcAddress(steam->module, "SteamAPI_ISteamUserStats_GetAchievement");
	steam->set_achievement = (void *)GetProcAddress(steam->module, "SteamAPI_ISteamUserStats_SetAchievement");
	steam->store_stats = (void *)GetProcAddress(steam->module, "SteamAPI_ISteamUserStats_StoreStats");

	return steam->init && steam->shutdown && steam->run_callbacks && steam->user_stats && steam->request_current_stats
		&& steam->get_achievement && steam->set_achievement && steam->store_stats;
}

static void pump(const Steam *steam, DWORD duration)
{
	for (DWORD elapsed = 0; elapsed < duration; elapsed += PUMP_INTERVAL_MS) {
		steam->run_callbacks();
		Sleep(PUMP_INTERVAL_MS);
	}
}

static bool unlock(const Steam *steam, const int *counters)
{
	SetEnvironmentVariableW(L"SteamAppId", APP_ID);
	SetEnvironmentVariableW(L"SteamGameId", APP_ID);

	if (!steam->init()) {
		print(L"Steam did not accept the connection. Start Steam, close the game, then try again.\n");
		return false;
	}

	void *stats = steam->user_stats();
	bool achieved = false;
	bool ready = false;

	if (stats) {
		steam->request_current_stats(stats);

		for (DWORD elapsed = 0; elapsed < STATS_TIMEOUT_MS && !ready; elapsed += PUMP_INTERVAL_MS) {
			pump(steam, PUMP_INTERVAL_MS);
			ready = steam->get_achievement(stats, ACHIEVEMENTS[0].id, &achieved);
		}
	}

	if (!ready) {
		print(L"Steam did not return your achievement data.\n");
		steam->shutdown();
		return false;
	}

	bool changed = false;
	bool success = true;

	for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
		const Achievement *achievement = &ACHIEVEMENTS[i];
		if (counters[i] < achievement->target)
			continue;

		if (steam->get_achievement(stats, achievement->id, &achieved) && achieved) {
			print(L"  %-26ls already unlocked\n", achievement->title);
			continue;
		}

		if (steam->set_achievement(stats, achievement->id)) {
			print(L"  %-26ls unlocked\n", achievement->title);
			changed = true;
		} else {
			print(L"  %-26ls FAILED\n", achievement->title);
			success = false;
		}
	}

	if (changed) {
		success = steam->store_stats(stats) && success;
		pump(steam, STORE_WAIT_MS);
	}

	steam->shutdown();
	return success;
}

static int finish(int code)
{
	print(L"\nPress a key to close.\n");
	_getwch();
	return code;
}

int wmain(int argc, wchar_t **argv)
{
	print(L"fix-vanguard: unlocks the Call of Duty: Vanguard campaign achievements that you earned in the game.\n\n");

	wchar_t settings_path[PATH_SIZE];
	if (!find_settings_file(settings_path)) {
		print(L"No settings file found in Documents\\%ls.\nStart the game one time, then try again.\n", SETTINGS_FOLDER);
		return finish(1);
	}

	DWORD size;
	uint8_t *data = read_file(settings_path, &size);
	int counters[ACHIEVEMENT_COUNT];

	if (!data || !read_counters(data, size, counters)) {
		print(L"Could not read the campaign progress from:\n%ls\nThe game version is possibly different.\n", settings_path);
		return finish(1);
	}

	free(data);
	print(L"Campaign progress from:\n%ls\n\n", settings_path);

	int earned = 0;
	for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
		bool done = counters[i] >= ACHIEVEMENTS[i].target;
		earned += done;
		print(L"  %-26ls %5d / %-3d %ls\n", ACHIEVEMENTS[i].title, counters[i], ACHIEVEMENTS[i].target, done ? L"EARNED" : L"");
	}

	if (earned == 0) {
		print(L"\nNo achievement is earned at this time.\n");
		return finish(0);
	}

	print(L"\nUnlock the %d earned achievement(s) on Steam? (Y/N) ", earned);
	wint_t key = _getwch();
	print(L"\n\n");

	if (key != L'y' && key != L'Y')
		return finish(0);

	wchar_t dll_path[PATH_SIZE];
	if (!find_steam_dll(argc > 1 ? argv[1] : NULL, dll_path)) {
		print(L"Could not find %ls.\nPut this program in the game folder, or give the game folder as the first argument.\n", STEAM_DLL);
		return finish(1);
	}

	Steam steam;
	if (!load_steam(dll_path, &steam)) {
		print(L"Could not load:\n%ls\n", dll_path);
		return finish(1);
	}

	if (!unlock(&steam, counters))
		return finish(1);

	print(L"\nDone.\n");
	return finish(0);
}
