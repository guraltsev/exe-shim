from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain


class ExeShimConan(ConanFile):
    name = "exe-shim"
    version = "1.0.0"
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    requires = "fmt/11.1.4"

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        cmake = CMake(self)
        cmake.test()
