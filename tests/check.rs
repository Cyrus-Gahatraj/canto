mod common;
use common::CantoTest;

#[test]
fn exprs_test() {
    CantoTest::new("exprs.ct").assert_output("15");
}

