@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall" x64
IF NOT EXIST build (mkdir build)
pushd build
IF NOT EXIST files_raw (mkdir files_raw)
IF NOT EXIST files_cmp (mkdir files_cmp)
cl -nologo -GR- -W4 -WX -wd4146 -wd4819 -wd4100 -wd4458 -Oi -Zi /FC /EHa "..\main.cpp"
popd