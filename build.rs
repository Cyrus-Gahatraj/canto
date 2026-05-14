fn main() {
    cc::Build::new()
        .include("include")
        .include("include/private")
        .file("src/c/memory.c") 
        .file("src/c/lexer.c") 
        .warnings(true)
        .compile("canto");

    println!("cargo:rustc-link-lib=static=canto");
}

