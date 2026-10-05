mod common;
use common::CantoTest;

#[test]
fn condtion_test() {
    CantoTest::new("conditional.ct").assert_output("Thou art more lovely than a summer day");
}

#[test]
fn exprs_test() {
    CantoTest::new("exprs.ct").assert_output("15");
}

#[test]
fn edits_test() {
    CantoTest::new("edits.ct").assert_output("Accessing: /usr/var/script.sh\nSecurity Mask: 700");
}

#[test]
fn loops_test() {
    CantoTest::new("loops.ct").assert_output(
        r#"What did you dream?
It's alright, we know what you dream
Welcome my son
Welcome to the machine"#,
    );
}

#[test]
fn functions_test() {
    CantoTest::new("functions.ct").assert_output("25");
}

#[test]
fn keyword_modifiers_test() {
    CantoTest::new("keyword_modifiers.ct").assert_output("hello world");
}

#[test]
fn arrays_test() {
    CantoTest::new("arrays.ct").assert_output("Hello\nArray");
}

#[test]
fn when_test() {
    // Tests integer equality, predicate (dot) arms, and string equality
    CantoTest::new("when.ct").assert_output("two\nC\nformal");
}

#[test]
fn get_test() {
    CantoTest::new("get.ct").assert_output("4.000000\n1024.000000\n43\n5\nhello from libc");
}

#[test]
fn modules_test() {
    CantoTest::new("modules.ct").assert_output("1764\n4.000000");
}

#[test]
fn module_cycle_test() {
    CantoTest::new("module_cycle.ct").assert_output("pong\nping\ndone");
}

#[test]
fn module_missing_test() {
    CantoTest::new("module_missing.ct").assert_compile_error("cannot find module");
}

#[test]
fn module_block_test() {
    CantoTest::new("module_block.ct").assert_compile_error("a Canto module takes no '{ }' block");
}

#[test]
fn functions_any_test() {
    CantoTest::new("functions_any.ct")
        .assert_output("5\n3.750000\n2.500000\ntrue\nyes\n2.500000\n6.000000\nloud\ntrue");
}

#[test]
fn functions_mixed_return_test() {
    CantoTest::new("functions_mixed_return.ct")
        .assert_compile_error("returns must all have the same type");
}
