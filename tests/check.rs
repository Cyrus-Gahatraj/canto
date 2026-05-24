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
fn keyword_modifiers_test() {
    CantoTest::new("keyword_modifiers.ct").assert_output("hello world");
}

