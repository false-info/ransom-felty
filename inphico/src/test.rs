use walkdir::WalkDir;
use std::io::Write;
use std::fs::File;

pub fn test_it() -> std::io::Result<()> {
    let mut destination = File::create("/tmp/output.txt")?;
    let mut n = 0;
    
    for entry in WalkDir::new("/") {
        let entry = entry.unwrap();
        let path = entry.path();
        
        if path.is_file() {
            writeln!(destination, "{}", path.display())?;
            n += 1;
        }
    }
    println!("copied {} files", n);
    Ok(())
}
