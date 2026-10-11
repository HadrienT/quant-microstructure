import re

import quantmicrostructure


def test_version_matches_build_info():
    info = quantmicrostructure.build_info()
    assert re.fullmatch(r"\d+\.\d+\.\d+", info["version"])
    assert quantmicrostructure.__version__ == info["version"]


def test_build_info_names_the_commit():
    info = quantmicrostructure.build_info()
    assert set(info) == {"version", "git_sha"}
    assert re.fullmatch(r"[0-9a-f]{40}(-dirty)?|unknown", info["git_sha"])
