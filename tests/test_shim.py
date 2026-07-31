"""End-to-end tests for the CMake-built Windows shim executable."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


SHIM_BINARY_ENVIRONMENT_VARIABLE = "EXE_SHIM_BINARY"
RECORDER_BINARY_ENVIRONMENT_VARIABLE = "EXE_SHIM_RECORDER"


class TestShimIntegration(unittest.TestCase):
    """Run a compiled shim against disposable shim files and target programs."""

    @classmethod
    def setUpClass(cls) -> None:
        shim_binary = os.environ.get(SHIM_BINARY_ENVIRONMENT_VARIABLE)
        recorder_binary = os.environ.get(RECORDER_BINARY_ENVIRONMENT_VARIABLE)
        if shim_binary is None or recorder_binary is None:
            cls.compiler_error = (
                "Set EXE_SHIM_BINARY and EXE_SHIM_RECORDER by running CTest after a CMake build."
            )
            return

        cls.tempdir = tempfile.TemporaryDirectory(prefix="exe-shim-tests-")
        cls.workdir = Path(cls.tempdir.name)
        cls.shim_binary = Path(shim_binary)
        cls.target = cls.workdir / "target with spaces" / "argument recorder.exe"
        cls.target.parent.mkdir()
        shutil.copy2(recorder_binary, cls.target)

    @classmethod
    def tearDownClass(cls) -> None:
        if hasattr(cls, "tempdir"):
            cls.tempdir.cleanup()

    def setUp(self) -> None:
        if not hasattr(self, "workdir"):
            self.skipTest(self.compiler_error)

        self.case_dir = Path(tempfile.mkdtemp(dir=self.workdir, prefix="case-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.case_dir)

    def make_launcher(self, shim_contents: str | None) -> Path:
        # A spaced launcher name makes Windows quote argv[0], covering the
        # command-line parsing path used to separate the shim from its inputs.
        launcher = self.case_dir / "tool with spaces.exe"
        shutil.copy2(self.shim_binary, launcher)
        if shim_contents is not None:
            launcher.with_suffix(".shim").write_text(shim_contents, encoding="utf-8")
        return launcher

    def run_launcher(self, launcher: Path, *arguments: str, exit_code: int = 0) -> subprocess.CompletedProcess[str]:
        output = self.case_dir / "arguments.txt"
        environment = os.environ.copy()
        environment["SHIM_TEST_OUTPUT"] = str(output)
        environment["SHIM_TEST_EXIT_CODE"] = str(exit_code)
        result = subprocess.run(
            [str(launcher), *arguments],
            cwd=self.case_dir,
            env=environment,
            capture_output=True,
            text=True,
            timeout=15,
        )
        self.output = output
        return result

    def recorded_arguments(self) -> list[str]:
        return self.output.read_text(encoding="utf-8-sig").splitlines()

    def test_forwards_user_arguments_to_an_unquoted_path_with_spaces(self) -> None:
        launcher = self.make_launcher(f"path = {self.target}\n")

        result = self.run_launcher(launcher, "first value", "--flag=value")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.recorded_arguments()[1:], ["first value", "--flag=value"])

    def test_places_configured_arguments_before_user_arguments(self) -> None:
        launcher = self.make_launcher(
            f"path = {self.target}\nargs = --fixed value\n"
        )

        result = self.run_launcher(launcher, "--user", "two words")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self.recorded_arguments()[1:],
            ["--fixed", "value", "--user", "two words"],
        )

    def test_preserves_the_target_exit_code(self) -> None:
        launcher = self.make_launcher(f"path = {self.target}\n")

        result = self.run_launcher(launcher, "ignored", exit_code=37)

        self.assertEqual(result.returncode, 37, result.stderr)

    def test_reports_a_missing_shim_file(self) -> None:
        launcher = self.make_launcher(None)

        result = self.run_launcher(launcher)

        self.assertEqual(result.returncode, 1)
        self.assertIn("Cannot open shim file", result.stderr)

    def test_reports_a_shim_without_a_path(self) -> None:
        launcher = self.make_launcher("args = --fixed\nunknown = ignored\n")

        result = self.run_launcher(launcher)

        self.assertEqual(result.returncode, 1)
        self.assertIn("Could not read shim file", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
