# Felty Ransomware (C)

**⚠️ EDUCATIONAL USE ONLY.** This is a ransomware proof‑of‑concept. Using it against systems you do not own is illegal. The author accepts no liability.

## What It Does

1. **Encrypts files** with AES‑256 (CBC) – each file gets a unique random key.
2. **Wraps each AES key** with an embedded RSA‑4096 public key (only the attacker can decrypt).
3. **Deletes Volume Shadow Copies** (`vssadmin delete shadows /all`).
4. **Shows a fake CHKDSK screen** during encryption, then forces a reboot.
5. **Before login** (via `BootExecute`), a native boot app takes over:
   - **First reboot:** a hidden 60‑second timer runs while the screen slowly darkens. Then it displays *"don't you remember? everything is encrypted"* and freezes.
   - **Subsequent boots:** immediately shows *"nothing can save your files now"* and loops forever, blocking login.
6. **If the desktop is reached**, a full‑screen lock window hooks the keyboard and demands `felty-unlock` to close.

## Components
__________________________________________________________________________________________
|      File        |                          Purpose                                     |
|------------------|----------------------------------------------------------------------|
| `felty_loader.c` | Main orchestrator – encrypts, installs persistence, triggers reboot. |
| `felty_boot.c`   | Native NT application that runs before login and locks the machine.  |
| `felty_binder.c` | Post‑login lock window that blocks input and covers the desktop.      |
| `felty_decrypt.c`| Decryptor tool – uses the attacker's private RSA key to restore files.|
| `felty_keygen.py`| Generates RSA‑2048 keypair (`attacker_private.pem`, `attacker_pubkey.h`). |
| `makefile`       | Cross‑compiles all `.exe` files with `mingw-w64`.                      |

## Build (CachyOS / Arch)

```bash
sudo pacman -S mingw-w64-gcc make python python-pip
pip install cryptography
python3 felty_keygen.py
make
