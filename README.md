# ransom-felty
Nope
Bruhh

# so what you will need for this
windows vm 7-11

# Install tools
sudo pacman -S mingw-w64-gcc python-pip
pip install cryptography

# Generate keys
python3 felty_keygen.py

# Compile
make

# Deploy to target
cp felty.exe felty_decrypt.exe attacker_private.pem /path/to/vm_share/