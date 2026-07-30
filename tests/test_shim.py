"""End-to-end tests for the Windows shim executable.

The tests compile the launcher and a small argument-recording target with the
Microsoft C compiler, then run copies of the launcher alongside generated shim
files.  They deliberately use only the Python standard library so a Developer
Command Prompt is the only prerequisite.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SHIM_SOURCE = PROJECT_ROOT / "shim.c"

RECORDER_SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

int wmain(int argc, wchar_t** argv)
{
  wchar_t output[MAX_PATH];
  FILE* file = NULL;

  if (GetEnvironmentVariableW(L"SHIM_TEST_OUTPUT", output, MAX_PATH) == 0)
    return 90;

  if (_wfopen_s(&file, output, L"w, ccs=UTF-8") != 0)
    return 91;

  for (int i = 0; i < argc; ++i)
    fwprintf(file, L"%ls\n", argv[i]);

  fclose(file);

  wchar_t exit_code[16];
  if (GetEnvironmentVariableW(L"SHIM_TEST_EXIT_CODE", exit_code, 16) != 0)
    return _wtoi(exit_code);

  return 0;
}
'''


class ShimIntegrationTests(unittest.TestCase):
    """Run a compiled shim against disposable shim files and target programs."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.compiler = shutil.which("cl")
        if cls.compiler is None:
            raise unittest.SkipTest(
                "MSVC 'cl' was not found; run these tests from a Visual Studio Developer Command Prompt."
            )

        cls.tempdir = tempfile.TemporaryDirectory(prefix="exe-shim-tests-")
        cls.workdir = Path(cls.tempdir.name)
        cls.shim_binary = cls.workdir / "shim.exe"
        cls.target = cls.workdir / "target with spaces" / "argument recorder.exe"
        cls.target.parent.mkdir()

        cls._compile(SHIM_SOURCE, cls.shim_binary)
        recorder_source = cls.workdir / "argument_recorder.c"
        recorder_source.write_text(RECORDER_SOURCE, encoding="utf-8")
        cls._compile(recorder_source, cls.target)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tempdir.cleanup()

    @classmethod
    def _compile(cls, source: Path, output: Path) -> None:
        result = subprocess.run(
            [cls.compiler, "/nologo", "/O1", f"/Fe{output}", str(source)],
            cwd=cls.workdir,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise AssertionError(
                f"Could not compile {source.name}:\n{result.stdout}\n{result.stderr}"
            )

    def setUp(self) -> None:
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
