#!/usr/bin/env python3
import sys
from pathlib import Path
from cryptography.hazmat.primitives import hashes, padding as sym_padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.backends import default_backend

EXT = ".felty"


def decrypt_file(path: Path, privkey):
    data = path.read_bytes()
    if len(data) < 4:
        return False
    ek_len = int.from_bytes(data[0:4], "big")
    if len(data) < 4 + ek_len + 16:
        return False
    ek = data[4:4+ek_len]
    iv = data[4+ek_len:4+ek_len+16]
    ct = data[4+ek_len+16:]
    aes_key = privkey.decrypt(
        ek,
        padding.OAEP(
            mgf=padding.MGF1(algorithm=hashes.SHA256()),
            algorithm=hashes.SHA256(),
            label=None
        )
    )
    cipher = Cipher(algorithms.AES(aes_key), modes.CBC(iv), backend=default_backend())
    decryptor = cipher.decryptor()
    dec = decryptor.update(ct) + decryptor.finalize()
    unpad = sym_padding.PKCS7(128).unpadder()
    try:
        pt = unpad.update(dec) + unpad.finalize()
    except Exception:
        return False
    outp = path.with_suffix("")
    outp.write_bytes(pt)
    path.unlink()
    return True


def walk_and_decrypt(root: Path, privkey):
    count = 0
    for p in root.rglob("*.felty"):
        print("Decrypt:", p)
        try:
            if decrypt_file(p, privkey):
                print("OK")
                count += 1
            else:
                print("FAILED")
        except Exception as e:
            print("ERR", e)
    print(f"Decrypted {count} files")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: felty_decrypt_linux.py <private.pem> <directory>")
        sys.exit(1)
    priv = Path(sys.argv[1])
    root = Path(sys.argv[2])
    if not priv.exists():
        print("Private key not found:", priv)
        sys.exit(1)
    if not root.exists():
        print("Directory not found:", root)
        sys.exit(1)
    pem = priv.read_bytes()
    privkey = serialization.load_pem_private_key(pem, password=None, backend=default_backend())
    walk_and_decrypt(root, privkey)
