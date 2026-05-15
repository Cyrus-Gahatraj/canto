use std::process::Command;
use std::path::Path;

fn main() {
    let gperf_input = "src/internal/keyword_lookup.gperf";
    let gperf_output = "src/internal/keyword_lookup.c";

    if Path::new(gperf_input).exists() {
        let status = Command::new("gperf")
            .arg("--output-file")
            .arg(gperf_output)
            .arg("-L")
            .arg("ANSI-C") // modern function prototypes
            .arg("-t")     // Includes  struct definition
            .arg("-C")     // Makes the lookup tables 'const' (better for performance)
            .arg("-N")
            .arg("lookup_keyword")
            .arg(gperf_input)
            .output()
            .expect("Failed to execute gperf");

        std::fs::write(gperf_output, status.stdout)
            .expect("Failed to write generated lookup code.");
    }
    println!("cargo:rerun-if-change={}", gperf_input);

    println!("cargo:rerun-if-changed=src/c/");
    cc::Build::new()
        .include("src")
        .include("include")
        .file("src/c/memory.c") 
        .file("src/c/lexer.c") 
        .file("src/c/compiler.c") 
        .warnings(true)
        .compile("canto");

    println!("cargo:rustc-link-lib=static=canto");
}

