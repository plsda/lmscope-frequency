@echo off

set commonCompilerFlags=-nologo -EHsc -O2 -Oi -WX -W4 -wd4201 -wd4505 -wd4100 -wd4189 -wd4091 -wd4456 -wd4311 -wd4302 -FC -Z7 
set commonLinkerFlags= -incremental:no -opt:ref user32.lib  

mkdir .\build
pushd .\build

rem (try to) get the absolute path for the dll and escape path separators \ (i.e. replace \ --> \\)
set dllName=lmscopeHook.dll
set absPath=%CD%\%dllName%
SET absPath=%absPath:\=^\\%

rem x64
cl %commonCompilerFlags% ..\lmscopeHook.cpp /LD /link %commonLinkerFlags%
cl %commonCompilerFlags% -DDLL_NAME=%absPath% ..\injector.cpp /link shlwapi.lib %commonLinkerFlags%
popd

