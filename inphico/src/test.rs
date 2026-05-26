use walkdir::WalkDir;
use std::io::Write;
use std::fs::File;

pub fn test_it() -> std::io::Result<()> {
	let mut destination = File::create("output.txt")?;
	let mut n = 0;
	for anal in WalkDir::new("../.."){
		let entry = anal.unwrap();
		let entry = entry.path();
		//println!("{}", entry.display());
		writeln!(destination, "{}", entry.display())?;
		n += 1;
	}
	println!("copied {} files", n);
	Ok(())
}
