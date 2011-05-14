nmake -f xom.mak -nologo
xcopy /y ..\..\build\bios32\bin\xom.efi \\tsclient\p\xom.efi
xcopy /y /q ..\..\build\bios32\bin\xom.* ..\..\..\sample\build\nt32\bin

