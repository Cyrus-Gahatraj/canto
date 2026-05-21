// tests/common/mod.rs
use std::process::Command;
use std::path::PathBuf;

pub struct CantoTest {
    file_path: String,
    binary: PathBuf,
}

impl CantoTest {

    pub fn new(filename: &str) -> Self {
        let mut bin_path_str = String::from("./target/debug/canto");
        if cfg!(target_os = "windows") {
            bin_path_str += ".exe";
        }
        
        let bin_path = PathBuf::from(bin_path_str);
        if !bin_path.exists() {
            panic!(
                "Canto binary not found at {:?}. Please run 'cargo build' before executing.\n",
                bin_path
            );
        }

        let path = "tests/sources/".to_owned() + filename; 

        Self {
            file_path: path,
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

    pub fn assert_output(&self, expected_output: &str) {
        let (compile_success, stderr) = self.compile();
        
        assert!(
            compile_success, 
            "Could not compile the file: {}\nDiagnostic:\n{}", 
            self.file_path, 
            stderr
        );

        let program_output = self.run();
        assert_eq!(
            program_output.trim(), 
            expected_output.trim(),
            "\nRuntime output mismatch for file: {}\n", 
            self.file_path
        );
    }
}
