from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class FastDdsBridgeConan(ConanFile):
    name = "fastdds-bridge"
    version = "1.0"
    package_type = "application"
    settings = "os", "arch", "compiler", "build_type"
    requires = (
        "fast-dds/3.4.3",
        "ixwebsocket/12.0.0",
        "yaml-cpp/0.8.0",
        "nlohmann_json/3.12.0",
    )
    tool_requires = "cmake/[>=3.27]"

    default_options = {
        "fast-dds/*:shared": True,
        "fast-cdr/*:shared": True,
    }

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeToolchain(self).generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
