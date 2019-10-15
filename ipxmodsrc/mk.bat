@echo off
del *.obj *.err *.exe
wcl -d0 -os -mt -zp1 -3 -bcl=dos ipxsetup.c ipxnet.c doomnet.c
if exist ipxsetup.exe upx --best ipxsetup.exe
