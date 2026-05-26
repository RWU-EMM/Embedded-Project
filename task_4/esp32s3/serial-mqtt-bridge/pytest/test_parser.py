import json
import re
import pytest

from test_vectors import TEST_CASES


@pytest.mark.esp32s3
@pytest.mark.generic
@pytest.mark.parametrize("case", TEST_CASES)
def test_parser(dut, case):

    #
    # wait until firmware boots
    #
    dut.expect("READY")

    #
    # send uart command
    #
    dut.write(case["input"] + "\n")

    #
    # check warning if expected
    #
    if "warning" in case:
        dut.expect(case["warning"])

    #
    # wait for json output line
    #
    dut.expect("JSON_OUTPUT:")

    #
    # read remaining uart output
    #
    output = dut.read()

    #
    # extract json
    #
    match = re.search(
        r'JSON_OUTPUT:(\{.*\})',
        output
    )

    assert match is not None, \
        f"No JSON found in output:\n{output}"

    #
    # parse json
    #
    actual_json = json.loads(match.group(1))

    #
    # compare dictionaries
    #
    assert actual_json == case["expected"], \
        (
            f"\nExpected:\n{case['expected']}"
            f"\nActual:\n{actual_json}"
        )