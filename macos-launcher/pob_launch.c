// Native macOS launcher for Path of Building (PoE2).
// Mirrors the Windows "Path of Building-PoE2.exe": loads the SimpleGraphic
// runtime library from the directory containing this executable and runs
// the PoB entry script with the script path as argv[0].
//
// Expected layout (dev install, executable placed in <repo>/runtime):
//   <repo>/runtime/pob-poe2           <- this launcher
//   <repo>/runtime/libSimpleGraphic.dylib
//   <repo>/runtime/SimpleGraphic/Fonts/...
//   <repo>/src/Launch.lua

#include <dlfcn.h>
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv)
{
	char exePath[PATH_MAX];
	uint32_t size = sizeof(exePath);
	if (_NSGetExecutablePath(exePath, &size) != 0) {
		fprintf(stderr, "pob-poe2: executable path too long\n");
		return 1;
	}
	char realExePath[PATH_MAX];
	if (!realpath(exePath, realExePath)) {
		fprintf(stderr, "pob-poe2: realpath failed for %s\n", exePath);
		return 1;
	}
	char execPath[PATH_MAX];
	snprintf(execPath, sizeof(execPath), "%s", realExePath);
	// dirname may modify its argument; realExePath is disposable here.
	const char* exeDir = dirname(realExePath);

	// GLFW loads EGL with dlopen("libEGL.dylib") — a leaf name resolved
	// through DYLD_LIBRARY_PATH, which dyld only reads at process start.
	// Re-exec once with it pointing here so the ANGLE dylibs are found.
	if (!getenv("POB_LAUNCHER_RELAUNCHED")) {
		setenv("POB_LAUNCHER_RELAUNCHED", "1", 1);
		const char* oldPath = getenv("DYLD_LIBRARY_PATH");
		char dyldPath[PATH_MAX * 2];
		if (oldPath && *oldPath) {
			snprintf(dyldPath, sizeof(dyldPath), "%s:%s", exeDir, oldPath);
		} else {
			snprintf(dyldPath, sizeof(dyldPath), "%s", exeDir);
		}
		setenv("DYLD_LIBRARY_PATH", dyldPath, 1);
		execv(execPath, argv);
		fprintf(stderr, "pob-poe2: re-exec failed\n");
		return 1;
	}

	// On Windows LuaJIT's default package.path contains "!\lua\?.lua" (exe-dir
	// relative); Unix LuaJIT has no "!" expansion and the engine chdirs to the
	// script directory, so point the module search paths at this directory.
	// A trailing ";;" splices in the compiled-in default paths.
	char luaPath[PATH_MAX * 2];
	snprintf(luaPath, sizeof(luaPath), "%s/lua/?.lua;%s/lua/?/init.lua;;", exeDir, exeDir);
	setenv("LUA_PATH", luaPath, 1);
	char luaCPath[PATH_MAX];
	snprintf(luaCPath, sizeof(luaCPath), "%s/?.so;;", exeDir);
	setenv("LUA_CPATH", luaCPath, 1);

	char dylibPath[PATH_MAX];
	snprintf(dylibPath, sizeof(dylibPath), "%s/libSimpleGraphic.dylib", exeDir);
	void* lib = dlopen(dylibPath, RTLD_NOW | RTLD_GLOBAL);
	if (!lib) {
		fprintf(stderr, "pob-poe2: failed to load runtime: %s\n", dlerror());
		return 1;
	}

	int (*RunLuaFileAsWin)(int, char**) =
		(int (*)(int, char**))dlsym(lib, "RunLuaFileAsWin");
	if (!RunLuaFileAsWin) {
		fprintf(stderr, "pob-poe2: missing entry point: %s\n", dlerror());
		return 1;
	}

	char scriptRaw[PATH_MAX];
	snprintf(scriptRaw, sizeof(scriptRaw), "%s/../src/Launch.lua", exeDir);
	char scriptPath[PATH_MAX];
	if (!realpath(scriptRaw, scriptPath)) {
		fprintf(stderr, "pob-poe2: cannot find %s\n", scriptRaw);
		return 1;
	}

	char** runArgv = (char**)malloc(sizeof(char*) * (argc + 1));
	if (!runArgv) {
		return 1;
	}
	runArgv[0] = scriptPath;
	for (int i = 1; i < argc; ++i) {
		runArgv[i] = argv[i];
	}
	runArgv[argc] = NULL;

	return RunLuaFileAsWin(argc, runArgv);
}
