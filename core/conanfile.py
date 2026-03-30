from pathlib import Path

from conan import ConanFile


class QodeLocCoreConan(ConanFile):
    name = "qodeloc-core"
    package_type = "application"

    settings = "os", "arch", "compiler", "build_type"
    generators = ("CMakeDeps", "CMakeToolchain")
    default_options = {
        "gtest/*:build_gmock": True,
        "spdlog/*:header_only": True,
        "spdlog/*:use_std_fmt": True,
    }

    requires = (
        "tree-sitter/0.24.3",
        "tree-sitter-cpp/0.23.4",
        "cpp-httplib/0.20.1",
        "gtest/1.16.0",
        "spdlog/1.17.0",
        "nlohmann_json/3.11.3",
        "duckdb/1.4.3",
        "opentelemetry-cpp/1.24.0",
    )

    def set_version(self):
        version_file = Path(self.recipe_folder).parent / "VERSION"
        self.version = version_file.read_text(encoding="utf-8").strip()
