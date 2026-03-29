@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
"C:\Program Files\JetBrains\CLion 2025.3\bin\cmake\win\x64\bin\cmake.exe" --build C:\Users\abdus\Projects\ailo\cmake-build-debug --target ailo
