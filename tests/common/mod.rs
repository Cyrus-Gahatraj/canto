// tests/common/mod.rs
use std::process::Command;
use std::path::PathBuf;

pub struct CantoTest {
    file_path: String,
    binary: PathBuf,
}

impl CantoTest {

    pub fn new(file_path: &str) -> Self {
        let mut path_str = String::from("./target/debug/canto");
        if cfg!(target_os = "windows") {
            path_str += ".exe";
        }
        
        let bin_path = PathBuf::from(path_str);
        if !bin_path.exists() {
            panic!(
                "Canto binary not found at {:?}. Please run 'cargo build' before executing.\n",
                bin_path
            );
        }

        Self {
            file_path: file_path.to_string(),
            binary: bin_path,
        }
    }


    pub fn compile(&self) -> (bool, String) {
        let output = Command::new(&self.binary)
            .arg("run")
            .arg(&self.file_path)
            .output()
            .expect("Failed to execute Canto compiler binary");
        
        let stderr = String::from_utf8_lossy(&output.stderr).to_string();
        (output.status.success(), stderr)
    }

    pub fn run(&self) -> String {
        let interpreter = "lli";
        let ir_path = "build/canto.ll"; 

        let output = Command::new(interpreter)
            .arg(ir_path)
            .output()
            .expect("Could not run lli. Be sure LLVM is installed.");

        String::from_utf8_lossy(&output.stdout).to_string()
    }
}
