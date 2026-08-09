@echo off
setlocal

pushd "%~dp0" || exit /b 1

set "toolchain_root=%EXE_SHIM_UCRT64_ROOT%"
if not defined toolchain_root if defined MSYS2_INSTALL_PATH set "toolchain_root=%MSYS2_INSTALL_PATH%\ucrt64"

if defined toolchain_root (
  if not exist "%toolchain_root%\bin\g++.exe" (
    echo error: UCRT64 toolkit not found at "%toolchain_root%".
    goto :toolchain_missing
  )
  set "PATH=%toolchain_root%\bin;%PATH%"
) else (
  where g++.exe >nul 2>nul || goto :toolchain_missing
)

where mingw32-make.exe >nul 2>nul
if errorlevel 1 (
  echo error: mingw32-make was not found on PATH.
  goto :toolchain_missing
)

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

conan profile path default >nul 2>nul
if errorlevel 1 (
  echo Creating the default Conan profile...
  conan profile detect --force || goto :failure
)

conan install . --output-folder=build --build=missing || goto :failure
cmake --preset conan-release || goto :failure
cmake --build --preset conan-release || goto :failure

echo.
echo Build complete. Application executables are in artifacts.
popd
exit /b 0

:toolchain_missing
echo Set EXE_SHIM_UCRT64_ROOT to the UCRT64 directory or MSYS2_INSTALL_PATH to its MSYS2 installation directory.
echo Alternatively, add the UCRT64 bin directory to PATH.
pause
popd
exit /b 1

:failure
set "build_exit=%errorlevel%"
popd
exit /b %build_exit%
