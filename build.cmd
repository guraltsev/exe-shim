@echo off
setlocal

rem Build from the script directory so relative Conan and CMake paths are stable.
pushd "%~dp0" || exit /b 1

rem Prefer a requested UCRT64 installation, then detect the actual compiler
rem selected from PATH. Keep the compiler's bin directory on PATH because its
rem cc1plus frontend loads DLLs from there.
set "toolchain_root=%EXE_SHIM_UCRT64_ROOT%"
if not defined toolchain_root if defined MSYS2_INSTALL_PATH set "toolchain_root=%MSYS2_INSTALL_PATH%\ucrt64"

if defined toolchain_root (
  if not exist "%toolchain_root%\bin\g++.exe" (
    echo error: UCRT64 toolkit not found at "%toolchain_root%".
    goto :toolchain_missing
  )
  set "PATH=%toolchain_root%\bin;%PATH%"
)

set "cxx_compiler="
for /f "delims=" %%I in ('where g++.exe 2^>nul') do if not defined cxx_compiler set "cxx_compiler=%%~fI"
if not defined cxx_compiler goto :toolchain_missing
for %%I in ("%cxx_compiler%") do set "compiler_bin=%%~dpI"
set "c_compiler=%compiler_bin%gcc.exe"
if not exist "%c_compiler%" goto :toolchain_missing
set "PATH=%compiler_bin%;%PATH%"

rem Let the available build tool choose the CMake generator instead of fixing
rem the project to MinGW Makefiles. Ninja is preferred when installed.
set "cmake_generator="
where ninja.exe >nul 2>nul && set "cmake_generator=Ninja"
if not defined cmake_generator (
  where mingw32-make.exe >nul 2>nul && set "cmake_generator=MinGW Makefiles"
)
if not defined cmake_generator goto :builder_missing

where conan >nul 2>nul
if errorlevel 1 (
  echo error: Conan 2 is required but was not found on PATH.
  popd
  exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
  echo error: CMake 3.23 or newer is required but was not found on PATH.
  popd
  exit /b 1
)

rem Conan supplies the configured dependency graph consumed by the CMake preset.
conan profile path default >nul 2>nul
if errorlevel 1 (
  echo Creating the default Conan profile...
  conan profile detect --force || goto :failure
)

rem A CMake cache cannot safely switch compiler or generator. Its directory is
rem generated output, so recreate it automatically when discovery changes.
set "cached_compiler="
set "cached_generator="
if exist ".build\CMakeCache.txt" (
  for /f "tokens=2 delims==" %%I in ('findstr /b /c:"CMAKE_CXX_COMPILER:FILEPATH=" ".build\CMakeCache.txt"') do set "cached_compiler=%%I"
  for /f "tokens=2 delims==" %%I in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" ".build\CMakeCache.txt"') do set "cached_generator=%%I"
)
set "refresh_build="
if defined cached_compiler if /i not "%cached_compiler%"=="%cxx_compiler%" set "refresh_build=1"
if defined cached_generator if /i not "%cached_generator%"=="%cmake_generator%" set "refresh_build=1"
if defined refresh_build (
  echo Recreating .build because the selected compiler or generator changed.
  rmdir /s /q ".build"
)

echo Using compiler: %cxx_compiler%
echo Using generator: %cmake_generator%
conan install . --output-folder=.build --build=missing -c "tools.cmake.cmaketoolchain:generator=%cmake_generator%" || goto :failure
cmake --preset conan-release -DCMAKE_C_COMPILER="%c_compiler%" -DCMAKE_CXX_COMPILER="%cxx_compiler%" || goto :failure
cmake --build --preset conan-release || goto :failure

echo.
echo Build complete. Application executables are in .artifacts.
popd
exit /b 0

:toolchain_missing
echo Set EXE_SHIM_UCRT64_ROOT to the UCRT64 directory or MSYS2_INSTALL_PATH to its MSYS2 installation directory.
echo Alternatively, add the UCRT64 bin directory to PATH.
pause
popd
exit /b 1

:builder_missing
echo error: no supported CMake builder was found. Install Ninja or add mingw32-make.exe to PATH.
pause
popd
exit /b 1

:failure
set "build_exit=%errorlevel%"
popd
exit /b %build_exit%
