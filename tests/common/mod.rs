use std::path::PathBuf;
use std::process::Command;

pub struct CantoTest {
    file_path: String,
    bin_output_path: PathBuf,
    compiler_binary: PathBuf,
}

impl CantoTest {
    pub fn new(filename: &str) -> Self {
        let bin_path = PathBuf::from(env!("CARGO_BIN_EXE_canto"));

        if !bin_path.exists() {
            panic!(
                "Canto driver binary missing at absolute target: {:?}",
                bin_path
            );
        }
        
        let path = "tests/sources/".to_owned() + filename;
        let base_name = filename.strip_suffix(".ct").unwrap_or(filename);
        let bin_output_path = PathBuf::from(format!("build/{}", base_name));

        Self {
            file_path: path,
            bin_output_path,
            compiler_binary: bin_path,
        }
    }

    pub fn compile(&self) -> (bool, String) {
        let output = Command::new(&self.compiler_binary)
            .arg("build")
            .arg(&self.file_path)
            .output()
            .expect("Failed to execute Canto compiler binary");

        let stderr = String::from_utf8_lossy(&output.stderr).to_string();
        (output.status.success(), stderr)
    }

    pub fn run(&self) -> String {
        if !self.bin_output_path.exists() {
            panic!(
                "Compiled target binary missing at: {:?}. Did compilation step fail?",
                self.bin_output_path
            );
        }

        let output = Command::new(&self.bin_output_path)
            .output()
            .expect("Failed to execute compiled native test target");

        String::from_utf8_lossy(&output.stdout).to_string()
    }

    pub fn assert_output(&self, expected_output: &str) {
        let (compile_success, stderr) = self.compile();

        assert!(
            compile_success,
            "Could not compile the file: {}\nDiagnostic:\n{}",
            self.file_path, stderr
        );

        let program_output = self.run();
        assert_eq!(
            program_output.trim(),
            expected_output.trim(),
            "\nRuntime execution output mismatch for file: {}\n",
            self.file_path
        );
    }
}
