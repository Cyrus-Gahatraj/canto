use std::process::Command;
use std::path::Path;

fn main() {
    let gperf_input = "src/internal/keyword_lookup.gperf";
    let gperf_ouput = "src/internal/keyword_lookup.c";

    if Path::new(gperf_input).exists() {
        let status = Command::new("gperf")
            .arg("-L")
            .arg("C")
            .arg("-t")
            .arg("-C")
            .arg("-N")
            .arg("lookup_keyword")
            .arg(gperf_input)
            .output()
            .expect("Failed to execute gperf, please make sure it is install");

        std::fs::write(gperf_ouput, status.stdout)
            .expect("Failed to write generated lookup code.");
    }
    println!("cargo:rerun-if-change={}", gperf_input);

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

