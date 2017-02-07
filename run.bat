@echo off

start "Compressing" build\main.exe - build\files_raw\daina.mp3 build\files_cmp\daina.cmp

rem start "Decompressing" build/main.exe + build/files_cmp/daina.cmp build/files_raw/daina_new.mp3