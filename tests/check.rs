mod common;
use common::CantoTest;

#[test]
fn condtion_test() {
    CantoTest::new("conditional.ct")
        .assert_output("Thou art more lovely than a summer day");
}

#[test]
fn exprs_test() {
    CantoTest::new("exprs.ct").assert_output("15");
}

