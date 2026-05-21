mod common;

use common::CantoTest;

#[test]
fn exprs_test() {
    let exprs = "tests/sources/exprs.ct";
    let canto = CantoTest::new(exprs);
    
    let (compile_success, stderr) = canto.compile();
    assert!(
        compile_success, 
        "Could not compile the file: {}\nDiagnostic: {}", 
        exprs, 
        stderr
    );

    let program_output = canto.run();

    assert_eq!(program_output.trim(), "15");
}
