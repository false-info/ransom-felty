use std::fs::{self, File};
use std::io::{self, BufRead, BufReader};
use std::path::Path;
use base64::{engine::general_purpose, Engine as _};

pub fn copy_it() -> io::Result<()> {
    fs::create_dir_all("folder")?;

    let original = File::open("output.txt")?;
    let reader = BufReader::new(original);

    for line in reader.lines() {
        let source_filename = line?;
        
        let path = Path::new(&source_filename);
        if !path.is_file() {
            continue; 
        }
        
        if let Some(os_filename) = path.file_name() {
            if let Some(filename_str) = os_filename.to_str() {
                
                let mut source_file = File::open(&source_filename)?;
                
                let mut obfuscated_name = obfuscate(filename_str);
                
                if obfuscated_name.len() > 200 {
                    obfuscated_name.truncate(200);
                }
                
                let destination_path = format!("folder/{}", obfuscated_name);
                let mut destination_file = File::create(destination_path)?;

                io::copy(&mut source_file, &mut destination_file)?;
            }
        }
    }

    Ok(())
}

fn obfuscate(filename: &str) -> String {
    general_purpose::STANDARD.encode(filename)
}
