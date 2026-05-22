mod ffi;

use ffi::Engine;
use clap::{ Parser, Subcommand };
use std::{
    io::{self, Write},
    error::Error,
    path::Path,
    option::Option,
};

/// Canto — a poetic, configurable programming language
/// where the language itself can be shaped by your project.
///
/// Run a .ct file or start an interactive REPL session.
#[derive(Parser)]
#[command(
    name    = "canto",
    version = "0.1.0",
    author  = "Cyrus Gahatraj",
    about   = "The Canto language CLI",
    long_about = "Canto is a poetic, configurable programming language.\n\
                  Run a .ct file with `canto run <file>`, or start the REPL by running `canto` with no arguments"
)]
struct Cli {
    #[command(subcommand)]
    command: Option<Commands>,
}

#[derive(Subcommand)]
enum Commands {
    /// Build a .ct source file to .ll
    ///
    /// Example:
    ///   canto build src/main.ct
    Build {
        /// Path to the .ct file to execute
        path: String,
    },
}

fn main() -> Result<(), Box<dyn Error>> {
    let cli = Cli::parse();
    let engine = Engine::new();

    std::fs::create_dir_all("build")?;
    match &cli.command {
        Some(Commands::Build { path }) => {
            let path: &Path = Path::new(path);

            if path.extension().and_then(|ext| ext.to_str()) != Some("ct") {
                eprintln!("Error: File must have a .ct extension");
                std::process::exit(65);
            }

            let file_stem = path.file_stem().and_then(|s| s.to_str()).unwrap_or("canto");
            let output_ll = format!("build/{}.ll", file_stem);


            read_file(engine, path, Path::new(&output_ll));
        },
        None => {
            run_repl(engine)?;
        }
    }

    Ok(())
}

/// Read and execute a .ct source file.
/// Exits with code 65 on compile error (bad data format),
/// or code 70 on runtime error (internal software error).
fn read_file(engine: Engine, path: &Path, output_ll: &Path) {
    if path.extension().and_then(|ext| ext.to_str()) != Some("ct") {
       eprintln!("Error: File must have a .ct extension");
       return;
    }

    let source = std::fs::read_to_string(path)
        .expect("Could not read file — check the path and try again");

    engine.run(&source, Option::Some(path), Option::Some(output_ll));
}

/// Start an interactive REPL session.
///
/// Type Canto expressions line by line and see results immediately.
/// Type `exit` or send EOF (Ctrl+D) to quit.
fn run_repl(engine: Engine) -> io::Result<()> {
    println!("Canto 0.1.0 — type 'exit' to exit");
    println!("────────────────────────────────────────────");

    let mut line  = String::new();
    let stdin = io::stdin();

    loop {
        print!("canto: ");
        io::stdout().flush()?;

        line.clear();

        if stdin.read_line(&mut line)? == 0 { break }

        let input = line.trim();

        if input == "exit"  { break    }
        if input.is_empty() { continue }

        engine.run(input, Option::None, Option::None);
    }

    println!("\nfarewell.");
    Ok(())
}

