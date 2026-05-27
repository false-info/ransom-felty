#!/usr/bin/env python3
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.backends import default_backend

key = rsa.generate_private_key(public_exponent=65537, key_size=2048, backend=default_backend())
priv = key.private_bytes(
    serialization.Encoding.PEM,
    serialization.PrivateFormat.TraditionalOpenSSL,
    serialization.NoEncryption()
)
pub = key.public_key().public_bytes(
    serialization.Encoding.PEM,
    serialization.PublicFormat.SubjectPublicKeyInfo
)
with open('attacker_private.pem','wb') as f:
    f.write(priv)
with open('attacker_public.pem','wb') as f:
    f.write(pub)
print('[+] Wrote attacker_private.pem and attacker_public.pem')
