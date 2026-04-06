import pytest
from labios_mcp.server import _apply_pipeline_op


def test_grep_matches():
    lines = ["hello world", "foo bar", "hello again"]
    result = _apply_pipeline_op("grep:hello", lines, "test.txt")
    assert len(result) == 2
    assert all("hello" in l for l in result)


def test_grep_regex():
    lines = ["error: bad", "warning: ok", "ERROR: also bad"]
    result = _apply_pipeline_op("grep:error", lines, "test.txt")
    assert len(result) == 2  # case insensitive


def test_grep_no_match():
    lines = ["hello world"]
    result = _apply_pipeline_op("grep:xyz", lines, "test.txt")
    assert result == []


def test_head_default():
    lines = [str(i) for i in range(100)]
    result = _apply_pipeline_op("head", lines, "test.txt")
    assert len(result) == 10


def test_head_with_count():
    lines = [str(i) for i in range(100)]
    result = _apply_pipeline_op("head:5", lines, "test.txt")
    assert len(result) == 5
    assert result == ["0", "1", "2", "3", "4"]


def test_tail_with_count():
    lines = [str(i) for i in range(100)]
    result = _apply_pipeline_op("tail:3", lines, "test.txt")
    assert result == ["97", "98", "99"]


def test_count():
    lines = ["a", "b", "c"]
    result = _apply_pipeline_op("count", lines, "test.txt")
    assert result == ["3"]


def test_wc():
    lines = ["hello world", "foo"]
    result = _apply_pipeline_op("wc", lines, "test.txt")
    assert "lines: 2" in result[0]
    assert "words: 3" in result[0]


def test_sort():
    lines = ["c", "a", "b"]
    result = _apply_pipeline_op("sort", lines, "test.txt")
    assert result == ["a", "b", "c"]


def test_uniq():
    lines = ["a", "b", "a", "c", "b"]
    result = _apply_pipeline_op("uniq", lines, "test.txt")
    assert result == ["a", "b", "c"]


def test_filter():
    lines = ["include this", "skip", "include that"]
    result = _apply_pipeline_op("filter:include", lines, "test.txt")
    assert len(result) == 2


def test_sample():
    lines = [str(i) for i in range(100)]
    result = _apply_pipeline_op("sample:5", lines, "test.txt")
    assert len(result) == 5


def test_unknown_op_passthrough():
    lines = ["a", "b"]
    result = _apply_pipeline_op("bogus:arg", lines, "test.txt")
    assert result == lines


def test_pipeline_chain():
    lines = ["TODO: fix auth", "normal line", "TODO: add tests", "another line"]
    lines = _apply_pipeline_op("grep:TODO", lines, "test.txt")
    lines = _apply_pipeline_op("sort", lines, "test.txt")
    assert len(lines) == 2
    assert lines[0].startswith("TODO: add")
