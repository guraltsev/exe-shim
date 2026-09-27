"""Exercise the launcher through real Windows processes and TOML files.

The test suite copies each binary into a temporary directory so relative paths,
configuration discovery, environment inheritance, and exit-code propagation
are tested from the same layout users see after creating a shim.
"""
from __future__ import annotations
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

FIXTURES = Path(__file__).parent / "fixtures"

class TestTomlConfiguredShims(unittest.TestCase):
    """Run behavioral tests against a built console shim and recorder target."""

    @classmethod
    def setUpClass(cls):
        """Create one temporary target tree shared by all test cases."""
        if not (shim := os.environ.get("EXE_SHIM_BINARY")) or not (recorder := os.environ.get("EXE_SHIM_RECORDER")):
            cls.compiler_error = "Run via CTest after building."; return
        cls.tempdir = tempfile.TemporaryDirectory(prefix="exe-shim-tests-")
        cls.workdir = Path(cls.tempdir.name); cls.shim_binary = Path(shim)
        cls.target = cls.workdir / "target with spaces" / "recorder.exe"; cls.target.parent.mkdir()
        shutil.copy2(recorder, cls.target)
    @classmethod
    def tearDownClass(cls):
        """Remove the shared temporary tree after the test class finishes."""
        if hasattr(cls, "tempdir"): cls.tempdir.cleanup()

    def setUp(self):
        """Give each test an isolated directory beside the copied target."""
        if not hasattr(self, "workdir"): self.skipTest(self.compiler_error)
        self.case_dir = Path(tempfile.mkdtemp(dir=self.workdir, prefix="case-"))

    def tearDown(self):
        """Remove the per-test directory after subprocesses have exited."""
        shutil.rmtree(self.case_dir)

    def fixture(self, name, **values):
        """Load a TOML fixture and substitute paths without losing TOML escapes."""
        text = (FIXTURES / name).read_text(encoding="utf-8")
        for key, value in values.items(): text = text.replace("{{" + key + "}}", str(value).replace("\\", "\\\\"))
        return text

    def launcher(self, config):
        """Copy the shim and optionally write its sibling configuration file."""
        path = self.case_dir / "tool with spaces.exe"; shutil.copy2(self.shim_binary, path)
        if config is not None: path.with_suffix(".config.toml").write_text(config, encoding="utf-8")
        return path

    def run_launcher(self, launcher, *args, environment=None, exit_code=0):
        """Run a shim with recorder output directed to the current case directory."""
        self.output = self.case_dir / "arguments.txt"; env = os.environ | {"SHIM_TEST_OUTPUT": str(self.output), "SHIM_TEST_EXIT_CODE": str(exit_code)}
        if environment: env.update(environment)
        return subprocess.run([str(launcher), *args], cwd=self.case_dir, env=env, capture_output=True, text=True, timeout=15)

    def arguments(self):
        """Return recorded arguments without the target path in argv[0]."""
        return self.output.read_text(encoding="utf-8-sig").splitlines()[1:]

    def context(self):
        """Read the recorder's working-directory and environment report."""
        return dict(x.split("=", 1) for x in Path(str(self.output) + ".context").read_text(encoding="utf-8-sig").splitlines())

    def test_order_default_forwarding_and_exit_code(self):
        """Configured arguments precede forwarded arguments and status is preserved."""
        result = self.run_launcher(self.launcher(self.fixture("arguments.toml", target=self.target)), "two words", "--user", exit_code=37)
        self.assertEqual(result.returncode, 37, result.stderr)
        self.assertEqual(self.arguments(), ["--fixed", "value with spaces", "two words", "--user"])
        self.assertEqual(Path(self.context()["cwd"]), self.case_dir)
    def test_target_directory_as_working_dir_is_opt_in(self):
        """The target directory becomes cwd only when explicitly requested."""
        config = self.fixture("target-dir-working-dir.toml", target=self.target)
        result = self.run_launcher(self.launcher(config))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(Path(self.context()["cwd"]), self.target.parent)
    def test_forward_false_and_relative_target_workdir(self):
        """Relative target and working-directory values resolve beside the config."""
        local = self.case_dir / "bin" / "recorder.exe"; local.parent.mkdir(); shutil.copy2(self.target, local); (self.case_dir / "work").mkdir()
        result = self.run_launcher(self.launcher(self.fixture("relative.toml")), "ignored")
        self.assertEqual(result.returncode, 0, result.stderr); self.assertEqual(self.arguments(), ["fixed"]); self.assertEqual(Path(self.context()["cwd"]), self.case_dir / "work")
    def test_expansion_child_environment_and_path_prepend(self):
        """Expansion, removal, replacement, and PATH prepending affect only the child."""
        config = self.fixture("environment.toml", target=self.target)
        result = self.run_launcher(self.launcher(config), environment={"TEST_TARGET": str(self.target), "TEST_ARG": "expanded", "SOURCE_VALUE": "source", "REMOVE_ME": "remove"})
        self.assertEqual(result.returncode, 0, result.stderr); self.assertEqual(self.arguments(), ["expanded"]); self.assertEqual(self.context()["SHIM_TEST_VALUE"], "source-child"); self.assertEqual(self.context()["REMOVE_ME"], "<missing>")
        result = self.run_launcher(self.launcher(self.fixture("path-prepend.toml", target=self.target)))
        self.assertEqual(result.returncode, 0, result.stderr); self.assertEqual(self.context()["PATH"], f"{self.case_dir / 'one'};{self.case_dir / 'two'};tail")
    def test_missing_malformed_and_invalid_schema_fail_before_launch(self):
        """Invalid configuration reports its file and never starts the target."""
        cases = [(None, "Missing configuration file"), ("target = [broken\n", "malformed TOML")]
        cases += [(self.fixture(x, target=self.target), None) for x in ["missing-target.toml", "unknown-key.toml", "bad-argument.toml", "duplicate-remove.toml", "set-remove-conflict.toml", "unset-variable.toml", "working-dir-conflict.toml"]]
        for config, expected in cases:
            with self.subTest(config=config):
                launcher = self.launcher(config); result = self.run_launcher(launcher)
                self.assertEqual(result.returncode, 1); self.assertIn(str(launcher.with_suffix(".config.toml")), result.stderr)
                if expected: self.assertIn(expected, result.stderr)
                self.assertFalse(self.output.exists())
if __name__ == "__main__": unittest.main(verbosity=2)
