"""Integration coverage for TOML-configured launchers; see MANUAL_TESTING.md."""
from __future__ import annotations
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

FIXTURES = Path(__file__).parent / "fixtures"

class TestTomlConfiguredShims(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (shim := os.environ.get("EXE_SHIM_BINARY")) or not (recorder := os.environ.get("EXE_SHIM_RECORDER")):
            cls.compiler_error = "Run via CTest after building."; return
        cls.tempdir = tempfile.TemporaryDirectory(prefix="exe-shim-tests-")
        cls.workdir = Path(cls.tempdir.name); cls.shim_binary = Path(shim)
        cls.target = cls.workdir / "target with spaces" / "recorder.exe"; cls.target.parent.mkdir()
        shutil.copy2(recorder, cls.target)
    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "tempdir"): cls.tempdir.cleanup()
    def setUp(self):
        if not hasattr(self, "workdir"): self.skipTest(self.compiler_error)
        self.case_dir = Path(tempfile.mkdtemp(dir=self.workdir, prefix="case-"))
    def tearDown(self): shutil.rmtree(self.case_dir)
    def fixture(self, name, **values):
        text = (FIXTURES / name).read_text(encoding="utf-8")
        for key, value in values.items(): text = text.replace("{{" + key + "}}", str(value).replace("\\", "\\\\"))
        return text
    def launcher(self, config):
        path = self.case_dir / "tool with spaces.exe"; shutil.copy2(self.shim_binary, path)
        if config is not None: path.with_suffix(".config.toml").write_text(config, encoding="utf-8")
        return path
    def run(self, launcher, *args, environment=None, exit_code=0):
        self.output = self.case_dir / "arguments.txt"; env = os.environ | {"SHIM_TEST_OUTPUT": str(self.output), "SHIM_TEST_EXIT_CODE": str(exit_code)}
        if environment: env.update(environment)
        return subprocess.run([str(launcher), *args], cwd=self.case_dir, env=env, capture_output=True, text=True, timeout=15)
    def arguments(self): return self.output.read_text(encoding="utf-8-sig").splitlines()[1:]
    def context(self): return dict(x.split("=", 1) for x in Path(str(self.output) + ".context").read_text(encoding="utf-8-sig").splitlines())
    def test_order_default_forwarding_and_exit_code(self):
        result = self.run(self.launcher(self.fixture("arguments.toml", target=self.target)), "two words", "--user", exit_code=37)
        self.assertEqual(result.returncode, 37, result.stderr)
        self.assertEqual(self.arguments(), ["--fixed", "value with spaces", "two words", "--user"])
    def test_forward_false_and_relative_target_workdir(self):
        local = self.case_dir / "bin" / "recorder.exe"; local.parent.mkdir(); shutil.copy2(self.target, local); (self.case_dir / "work").mkdir()
        result = self.run(self.launcher(self.fixture("relative.toml")), "ignored")
        self.assertEqual(result.returncode, 0, result.stderr); self.assertEqual(self.arguments(), ["fixed"]); self.assertEqual(Path(self.context()["cwd"]), self.case_dir / "work")
    def test_expansion_child_environment_and_path_prepend(self):
        config = self.fixture("environment.toml", target=self.target)
        result = self.run(self.launcher(config), environment={"TEST_TARGET": str(self.target), "TEST_ARG": "expanded", "SOURCE_VALUE": "source", "REMOVE_ME": "remove"})
        self.assertEqual(result.returncode, 0, result.stderr); self.assertEqual(self.arguments(), ["expanded"]); self.assertEqual(self.context()["SHIM_TEST_VALUE"], "source-child"); self.assertEqual(self.context()["REMOVE_ME"], "<missing>")
        result = self.run(self.launcher(self.fixture("path-prepend.toml", target=self.target)))
        self.assertEqual(result.returncode, 0, result.stderr); self.assertEqual(self.context()["PATH"], f"{self.case_dir / 'one'};{self.case_dir / 'two'};tail")
    def test_missing_malformed_and_invalid_schema_fail_before_launch(self):
        cases = [(None, "Missing configuration file"), ("target = [broken\n", "malformed TOML")]
        cases += [(self.fixture(x, target=self.target), None) for x in ["missing-target.toml", "unknown-key.toml", "bad-argument.toml", "duplicate-remove.toml", "set-remove-conflict.toml", "unset-variable.toml"]]
        for config, expected in cases:
            with self.subTest(config=config):
                launcher = self.launcher(config); result = self.run(launcher)
                self.assertEqual(result.returncode, 1); self.assertIn(str(launcher.with_suffix(".config.toml")), result.stderr)
                if expected: self.assertIn(expected, result.stderr)
                self.assertFalse(self.output.exists())
if __name__ == "__main__": unittest.main(verbosity=2)
