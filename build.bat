@echo off
call "%vs110comntools%vsvars32.bat"
cl /nologo /GL /O2 /DWIN32_LEAN_AND_MEAN /Feloopback.exe *.cpp /link /fixed /ltcg /largeaddressaware kernel32.lib user32.lib ole32.lib
if errorlevel 1 pause
del *.obj
