fn main() {
    println!("cargo:rerun-if-changed=src/c/");

    cc::Build::new()
        .include("include")
        .include("include/private")
        .file("src/c/memory.c") 
        .file("src/c/lexer.c") 
        .file("src/c/compiler.c") 
        .warnings(true)
        .compile("canto");

    println!("cargo:rustc-link-lib=static=canto");
}

