use std::process::Command;
use std::path::Path;

fn main() {
    let gperf_input = "src/internal/keyword_lookup.gperf";
    let gperf_output = "src/internal/keyword_lookup.c";

    if Path::new(gperf_input).exists() {
        Command::new("gperf")
            .arg("--output-file")
            .arg(gperf_output)
            .arg("-L").arg("ANSI-C")
            .arg("-t")
            .arg("-C")
            .arg("-c")
            .arg("-N").arg("lookup_keyword")
            .arg("-E")
            .arg(gperf_input)
            .status()
            .expect("Failed to execute gperf");
        
    }
    println!("cargo:rerun-if-change={}", gperf_input);

    println!("cargo:rerun-if-changed=src/c/");
    cc::Build::new()
        .include("src")
        .include("include")
        .file("src/c/memory.c") 
        .file("src/c/lexer.c") 
        .file("src/c/compiler.c") 
        .file("src/c/symtable.c") 
        .file("src/c/diagnostic.c") 
        .file("src/c/span.c") 
        .file("src/c/source_map.c") 
        .file("src/c/parser.c") 
        .file("src/c/arena.c") 
        .warnings(true)
        .compile("canto");
    println!("cargo:rustc-link-lib=static=canto");

    let llvm_include = Command::new("llvm-config")
        .arg("--includedir")
        .output()
        .expect("llvm-config not found");

    let llvm_include =
        String::from_utf8(llvm_include.stdout)
            .unwrap()
            .trim()
            .to_string();

    cc::Build::new()
        .cpp(true)
        .file("src/llvm/codegen.cpp")
        .include("include")
        .include(&llvm_include)
        .flag("-std=c++20")
        .compile("canto_codegen");

}

