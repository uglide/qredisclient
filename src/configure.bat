@echo off

if exist nuget.exe (
    echo Nuget found
) else (
    echo Download nuget cmd utility
    powershell -Command "(New-Object Net.WebClient).DownloadFile('https://nuget.org/nuget.exe', 'nuget.exe')"
)

echo Install deps with nuget
nuget install -Version 1.8.0 -OutputDirectory ./../3rdparty/windows rmt_libssh2

