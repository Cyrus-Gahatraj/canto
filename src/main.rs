mod ffi;

use ffi::Engine;
use clap::{ Parser, Subcommand };
use std::{
    io::{self, Write},
    error::Error,
    path::Path,
    option::Option,
    process,
};

/// Canto — a poetic, configurable programming language
/// where the language itself can be shaped by your project.
///
/// Run a .ct file or start an interactive REPL session.
#[derive(Parser)]
#[command(
    name    = "canto",
    version = env!("CARGO_PKG_VERSION"),
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

    Run {
        path: String,
    },

    /// Create a new Canto project
    ///
    /// Example:
    ///   canto init hello
    Init {
        /// Folder to create the project in (default: the current folder)
        path: Option<String>,
    },

    /// Update canto to the latest version
    ///
    /// Re-runs install.sh, which rebuilds from the main branch
    /// (or $CANTO_BRANCH).
    Update,
}

const INSTALL_URL: &str = "https://raw.githubusercontent.com/Cyrus-Gahatraj/canto/main/install.sh";

/// Rebuilds canto from the latest source with the install script.
/// On Windows canto runs inside WSL, so this always runs under sh.
fn update() -> Result<(), Box<dyn Error>> {
    println!("Updating canto {} ...", env!("CARGO_PKG_VERSION"));
    let status = process::Command::new("sh")
        .arg("-c")
        .arg(format!("curl -fsSL {} | sh", INSTALL_URL))
        .status()?;
    if !status.success() {
        return Err("the install script failed".into());
    }
    Ok(())
}

/// Files `canto init` writes, from templates/init/
const INIT_FILES: &[(&str, &str)] = &[
    ("main.ct",          include_str!("../templates/init/main.ct")),
    ("editor/write.ct",  include_str!("../templates/init/editor/write.ct")),
    ("editor/types.ct",  include_str!("../templates/init/editor/types.ct")),
];

/// Writes a new project into `dir`. Refuses to overwrite existing files.
fn init_project(dir: &Path) -> Result<(), Box<dyn Error>> {
    for (name, _) in INIT_FILES {
        if dir.join(name).exists() {
            return Err(format!("{} already exists", dir.join(name).display()).into());
        }
    }
    for (name, contents) in INIT_FILES {
        let path = dir.join(name);
        std::fs::create_dir_all(path.parent().unwrap())?;
        std::fs::write(&path, contents)?;
    }
    println!("Created a Canto project in {}", dir.display());
    println!("Run it with:  canto run {}", dir.join("main.ct").display());
    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    let cli = Cli::parse();

    match &cli.command {
        Some(Commands::Build { path }) => {
            let _ = build_executable(path.to_string(), false);
        },
        Some(Commands::Run { path }) => {
            let _ = build_executable(path.to_string(), true);
        },
        Some(Commands::Update) => {
            if let Err(e) = update() {
                eprintln!("Error: {}", e);
                process::exit(1);
            }
        },
        Some(Commands::Init { path }) => {
            if let Err(e) = init_project(Path::new(path.as_deref().unwrap_or("."))) {
                eprintln!("Error: {}", e);
                process::exit(1);
            }
        },
        None => {
            let mut engine = Engine::new(true);
            run_repl(&mut engine)?;
        }
    }

    Ok(())
}

/// Build the executable
fn build_executable(path: String, execute: bool) -> Result<(), Box<dyn Error>> {
    let build_dir = "build";
    let tmp_dir =  build_dir.to_owned() + "/.tmp";
    std::fs::create_dir_all(&tmp_dir)?;

    let path: &Path = Path::new(&path);

    if path.extension().and_then(|ext| ext.to_str()) != Some("ct") {
        eprintln!("Error: File must have a .ct extension");
        process::exit(65);
    }

    let file_stem = path.file_stem().and_then(|s| s.to_str()).unwrap_or("canto");
    let tmp_ll = format!("{}/{}.ll", tmp_dir, file_stem);

    let extension = if cfg!(target_os = "windows") {
        ".exe"
    } else {
        "" 
    };
    let bin_out = format!("{}/{}{}", build_dir, file_stem, extension);

    let mut engine = Engine::new(false);
    read_file(&mut engine, path, Path::new(&tmp_ll))?;

    let status = process::Command::new("clang")
        .arg(&tmp_ll)
        .arg("-O2") // optimization
        .arg("-Wno-unused-command-line-argument") // Quiets unused args warning
        .arg("-Wno-override-module") // Quiets target triple overrides warning
        .arg("-o")
        .arg(&bin_out)
        .args(engine.link_libs())
        .status();

    match status {
       Ok(s) if s.success() => {
            if !execute {
                println!("Build Successful: {}", bin_out);
            }
       }
       _ => {
           eprintln!("Linking Error: failed to assemble native binary execuatable");
           process::exit(70);
       } 
    }

    let _ = std::fs::remove_dir_all(tmp_dir);

    if execute {
        let run_path = format!("./{}", bin_out);
        let run_status = process::Command::new(&run_path).status();
        
        match run_status {
            Ok(s) => {
                if !s.success() {
                    process::exit(s.code().unwrap_or(1));
                }
            }
            Err(_) => {
                eprintln!("Runtime Error: failed to launch execution binary target");
                process::exit(71);
            }
        }
    }

    Ok(())
}

/// Read and execute a .ct source file.
/// Exits with code 65 on compile error (bad data format),
/// or code 70 on runtime error (internal software error).
fn read_file(engine: &mut Engine, path: &Path, output_ll: &Path) -> Result<(), Box<dyn Error>> {
    let source = std::fs::read_to_string(path)
        .map_err(|e| {
            eprintln!("Could not read file — check the path and try again");
            e
        })?;

    engine.run(&source, Some(path), Some(output_ll));
    Ok(())
}

/// Start an interactive REPL session.
///
/// Type Canto expressions line by line and see results immediately.
/// Type `exit` or send EOF (Ctrl+D) to quit.
fn run_repl(engine: &mut Engine) -> io::Result<()> {
    println!("Canto 0.1.0 — type 'exit' to exit");
    println!("────────────────────────────────────────────");

    let mut line = String::new();
    let stdin = io::stdin();

    loop {
        print!("canto: ");
        io::stdout().flush()?;

        line.clear();
        if stdin.read_line(&mut line)? == 0 { break; }

        let input = line.trim();
        if input == "exit" { break; }
        if input.is_empty() { continue; }

        // State successfully alters raw_ctx step by step!
        engine.run(input, None, None);
    }

    println!("\nfarewell.");
    Ok(())
}

