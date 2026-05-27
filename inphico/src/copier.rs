use std::fs::{self, File};
use std::io::{self, BufRead, BufReader, Read, Write};
use std::path::Path;
use base64::{engine::general_purpose, Engine as _};
use aes_gcm::{Aes256Gcm, KeyInit, aead::Aead};
use aes_gcm::aead::generic_array::GenericArray;
use rand::RngCore;

pub fn copy_it() -> io::Result<()> {
    fs::create_dir_all("folder")?;

    let original = File::open("output.txt")?;
    let reader = BufReader::new(original);

    // Convert your hex string hash directly into a raw 32-byte array encryption key
    let hex_hash = "1f8c16044419c48333715ae2a712e6e0a30722d3c56bc9f581496910af47df8d";
    let key_bytes = hex_to_bytes(hex_hash).map_err(|_| {
        io::Error::new(io::ErrorKind::InvalidInput, "Invalid hex hash string length or character")
    })?;

    for line in reader.lines() {
        let source_filename = line?;
        
        let path = Path::new(&source_filename);
        if !path.is_file() {
            continue; 
        }
        
        if let Some(os_filename) = path.file_name() {
            if let Some(filename_str) = os_filename.to_str() {
                
                // 1. Open and read the raw bytes of the target source file
                let mut source_file = File::open(&source_filename)?;
                let mut raw_bytes = Vec::new();
                source_file.read_to_end(&mut raw_bytes)?;
                
                // 2. Obfuscate target filename
                let mut obfuscated_name = obfuscate(filename_str);
                if obfuscated_name.len() > 200 {
                    obfuscated_name.truncate(200);
                }
                
                let destination_path = format!("folder/{}", obfuscated_name);
                let mut destination_file = File::create(destination_path)?;

                // 3. Generate a fresh, random 12-byte Nonce/IV for this specific file
                let mut nonce = [0u8; 12];
                rand::thread_rng().fill_bytes(&mut nonce);

                // 4. Encrypt using AES-256-GCM
                match encrypt(&raw_bytes, &key_bytes, &nonce) {
                    Ok(ciphertext) => {
                        // Crucial: Write the 12-byte nonce to the file first so you can decrypt it later
                        destination_file.write_all(&nonce)?;
                        destination_file.write_all(&ciphertext)?;
                        destination_file.flush()?;
                    }
                    Err(_) => {
                        return Err(io::Error::new(io::ErrorKind::Other, "Encryption failed"));
                    }
                }
            }
        }
    }

    Ok(())
}

fn obfuscate(filename: &str) -> String {
    general_purpose::URL_SAFE.encode(filename)
}

fn encrypt(data: &[u8], key: &[u8; 32], nonce: &[u8; 12]) -> Result<Vec<u8>, aes_gcm::Error> {
    let key_ga = GenericArray::from_slice(key);
    let nonce_ga = GenericArray::from_slice(nonce);
    
    let cipher = Aes256Gcm::new(key_ga);
    let ciphertext = cipher.encrypt(nonce_ga, data)?;
    Ok(ciphertext)
}

// Helper function to decode your hex hash string into raw bytes ([u8; 32])
fn hex_to_bytes(hex: &str) -> Result<[u8; 32], &'static str> {
    if hex.len() != 64 {
        return Err("Hex string must be exactly 64 characters long for a 32-byte key");
    }
    
    let mut bytes = [0u8; 32];
    for i in 0..32 {
        let idx = i * 2;
        let slice = &hex[idx..idx + 2];
        bytes[i] = u8::from_str_radix(slice, 16).map_err(|_| "Invalid hex character")?;
    }
    Ok(bytes)
}
