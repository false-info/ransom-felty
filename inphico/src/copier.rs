use std::fs::File;
use std::io::{self, BufRead, BufReader};
use base64::{engine::general_purpose, Engine as _};

pub fn copy_it() -> io::Result<()> {
    let original = File::open("output.txt")?;
    let reader = BufReader::new(original);

    for line in reader.lines() {
        let source_filename = line?;
        
        let mut source_file = File::open(&source_filename)?;
        
        let destination_path = format!("folder/{}", obfuscate(&source_filename));
        
        let mut destination_file = File::create(destination_path)?;

        io::copy(&mut source_file, &mut destination_file)?;
    }
	println!("Done copying files");
    Ok(())
}

fn obfuscate(filename: &str) -> String {
    general_purpose::STANDARD.encode(filename)
}
