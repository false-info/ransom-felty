CC = x86_64-w64-mingw32-gcc
CFLAGS = -Os -s -Wall -fno-asynchronous-unwind-tables
LIBS = -lcrypt32 -ladvapi32 -lws2_32 -luuid -liphlpapi -lnetapi32

all: felty.exe felty_decrypt.exe

felty.exe: felty.c attacker_pubkey.h
	$(CC) $(CFLAGS) -o felty.exe felty.c $(LIBS)
	strip --strip-all felty.exe
	@echo "[+] Built: felty.exe"

felty_decrypt.exe: felty_decrypt.c
	$(CC) $(CFLAGS) -o felty_decrypt.exe felty_decrypt.c -lcrypt32 -ladvapi32
	strip --strip-all felty_decrypt.exe
	@echo "[+] Built: felty_decrypt.exe"

clean:
	rm -f felty.exe felty_decrypt.exe *.o

.PHONY: all clean