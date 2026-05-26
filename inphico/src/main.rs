mod copier;
mod test;
//use std::fs;


fn main() {
    let _ = test::test_it();
    copier::copy_it().expect("Failed to copy files");
}
