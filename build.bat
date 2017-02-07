@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall" x64
pushd build
cl -nologo -GR- -W4 -WX -wd4100 -wd4458 -Oi -Zi /FC /EHa "..\main.cpp"
popd