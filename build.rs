use std::process::Command;
use std::path::Path;

fn main() {

    // gperf
    let gperf_input  = "src/internal/keyword_lookup.gperf";
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
    println!("cargo:rerun-if-changed={}", gperf_input);
    println!("cargo:rerun-if-changed=src/c/");
    println!("cargo:rerun-if-changed=src/llvm/");

    // C
    cc::Build::new()
        .include("src")
        .include("include")
        .files([
            "src/c/canto.c",
            "src/c/memory.c",
            "src/c/lexer.c",
            "src/c/compiler.c",
            "src/c/symtable.c",
            "src/c/diagnostic.c",
            "src/c/span.c",
            "src/c/source_map.c",
            "src/c/parser.c",
            "src/c/arena.c",
        ])
        .warnings(false)   // suppress warnings
        .compile("canto_c");

    println!("cargo:rustc-link-lib=static=canto_c");

    // LLVM config
    let llvm_includedir = llvm_config(&["--includedir"]);
    let llvm_libdir     = llvm_config(&["--libdir"]);
    let llvm_libs       = llvm_config(&["--libs",
                                        "core",
                                        "executionengine",
                                        "mcjit",
                                        "native",
                                        "support",
                                        "target"]);
    let llvm_syslibs    = llvm_config(&["--system-libs"]);
    let llvm_cxxflags   = llvm_config(&["--cxxflags"]);

    // C++ codegen
    let mut build = cc::Build::new();
    build
        .cpp(true)
        .files([ 
            "src/llvm/codegen.cpp",
            "src/llvm/context.cpp",
            "src/llvm/expr_gen.cpp",
            "src/llvm/stmt_gen.cpp",
        ])
        .include("include")
        .include(&llvm_includedir)
        .flag("-std=c++20")
        .flag("-Wno-unused-parameter");   // silence LLVM header warnings

    // pass LLVM cxxflags
    for flag in llvm_cxxflags.split_whitespace() {
        if flag.starts_with("-D") || flag.starts_with("-I") {
            build.flag(flag);
        }
    }

    build.compile("canto_codegen");
    println!("cargo:rustc-link-lib=static=canto_codegen");

    // link LLVM libraries
    println!("cargo:rustc-link-search=native={}", llvm_libdir.trim());

    for token in llvm_libs.split_whitespace() {
        if let Some(lib) = token.strip_prefix("-l") {
            println!("cargo:rustc-link-lib={}", lib);
        }
        if let Some(path) = token.strip_prefix("-L") {
            println!("cargo:rustc-link-search=native={}", path);
        }
    }

    // system libs 
    for token in llvm_syslibs.split_whitespace() {
        if let Some(lib) = token.strip_prefix("-l") {
            println!("cargo:rustc-link-lib={}", lib);
        }
    }

}

fn llvm_config(args: &[&str]) -> String {
    let out = Command::new("llvm-config")
        .args(args)
        .output()
        .expect("llvm-config not found — is LLVM installed?");

    if !out.status.success() {
        panic!("llvm-config failed: {}",
               String::from_utf8_lossy(&out.stderr));
    }

    String::from_utf8(out.stdout)
        .expect("llvm-config output is not UTF-8")
        .trim()
        .to_string()
}
