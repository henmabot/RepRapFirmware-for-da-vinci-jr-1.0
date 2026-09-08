#!/usr/bin/env python3
from __future__ import annotations

import argparse
import concurrent.futures
import fnmatch
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = ROOT / "build"

LIBRARIES = (
    ("FreeRTOS", "SAM4E"),
    ("RRFLibraries", "SAM4E_RTOS"),
    ("CoreN2G", "SAM4E_SDHC_USB_RTOS"),
    ("CANlib", "SAM4E_RTOS"),
)
FIRMWARE = ("RepRapFirmware", "Duet2_SBC")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp"}


@dataclass(frozen=True)
class BuildConfig:
    project: str
    name: str
    config: ET.Element

    @property
    def project_dir(self) -> Path:
        return ROOT / self.project

    @property
    def build_dir(self) -> Path:
        return BUILD_ROOT / self.project / self.name

    @property
    def artifact_name(self) -> str:
        value = self.config.get("artifactName") or self.project
        return value.replace("${ProjName}", self.project)


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def find_config(project: str, name: str) -> BuildConfig:
    root = ET.parse(ROOT / project / ".cproject").getroot()
    matches = [
        element
        for element in root.iter("configuration")
        if element.get("name") == name and element.get("artifactName") is not None
    ]
    if len(matches) != 1:
        raise RuntimeError(
            f"expected one {project}/{name} build configuration, found {len(matches)}"
        )
    return BuildConfig(project, name, matches[0])


def options(config: BuildConfig, prefix: str) -> list[ET.Element]:
    return [
        option
        for option in config.config.iter("option")
        if (option.get("superClass") or "").startswith(prefix)
    ]


def list_values(
    config: BuildConfig, key_fragment: str, language_prefix: str
) -> list[str]:
    values: list[str] = []
    for option in options(config, language_prefix):
        if key_fragment in (option.get("superClass") or ""):
            values.extend(
                value.get("value") or ""
                for value in option.findall("listOptionValue")
                if value.get("value")
            )
    return values


def scalar_values(
    config: BuildConfig, key_fragment: str, language_prefix: str
) -> list[str]:
    return [
        option.get("value") or ""
        for option in options(config, language_prefix)
        if key_fragment in (option.get("superClass") or "") and option.get("value")
    ]


def expand_workspace_path(value: str, config: BuildConfig) -> Path:
    value = value.strip('"').replace("${ProjName}", config.project)
    match = re.fullmatch(r"\$\{workspace_loc:/([^}]*)\}", value)
    if not match:
        raise RuntimeError(f"unsupported Eclipse workspace path: {value}")
    return ROOT / match.group(1)


def compiler_flags(config: BuildConfig, language: str) -> list[str]:
    prefix = "gnu.c.compiler" if language == "c" else "gnu.cpp.compiler"
    flags: list[str] = []

    optimization = scalar_values(config, "optimization.level", prefix)
    if any(value.endswith(".size") for value in optimization):
        flags.append("-Os")

    other_flags = "misc.other" if language == "c" else "other.other"
    for value in scalar_values(config, "dialect.flags", prefix) + scalar_values(
        config, other_flags, prefix
    ):
        for flag in shlex.split(value):
            if flag == "-c" or "$*" in flag:
                continue
            flags.append(flag)

    for define in list_values(config, "preprocessor.def", prefix):
        if '="' in define and define.endswith('"'):
            name, value = define.split("=", 1)
            define = f"{name}={value[1:-1]}"
        flags.append(f"-D{define}")
    for include in list_values(config, "include.paths", prefix):
        flags.extend(("-I", str(expand_workspace_path(include, config))))
    return flags


def source_exclusions(config: BuildConfig) -> tuple[str, ...]:
    exclusions: list[str] = []
    for entry in config.config.iter("entry"):
        if entry.get("kind") != "sourcePath":
            continue
        exclusions.extend(
            part.strip("/")
            for part in (entry.get("excluding") or "").split("|")
            if part
        )
    return tuple(exclusions)


def is_source_excluded(relative: Path, exclusions: tuple[str, ...]) -> bool:
    value = relative.as_posix()
    return any(
        value == exclusion or value.startswith(exclusion + "/")
        for exclusion in exclusions
    )


def project_filters(config: BuildConfig) -> dict[Path, list[tuple[int, str]]]:
    project_file = config.project_dir / ".project"
    if not project_file.exists():
        return {}
    result: dict[Path, list[tuple[int, str]]] = {}
    for filter_element in (
        ET.parse(project_file).getroot().findall("./filteredResources/filter")
    ):
        matcher = filter_element.find("matcher")
        if (
            matcher is None
            or matcher.findtext("id") != "org.eclipse.ui.ide.multiFilter"
        ):
            continue
        arguments = matcher.findtext("arguments") or ""
        match = re.fullmatch(
            r"1\.0-name-matches-(?:true|false)-(?:true|false)-(.+)", arguments
        )
        if match is None:
            raise RuntimeError(f"unsupported Eclipse resource filter: {arguments}")
        container = Path(filter_element.findtext("name") or ".")
        filter_type = int(filter_element.findtext("type") or "0")
        result.setdefault(container, []).append((filter_type, match.group(1)))
    return result


def is_resource_filtered(
    relative: Path, filters: dict[Path, list[tuple[int, str]]]
) -> bool:
    parts = relative.parts
    for container, rules in filters.items():
        container_parts = () if container == Path(".") else container.parts
        if parts[: len(container_parts)] != container_parts or len(parts) == len(
            container_parts
        ):
            continue
        child = parts[len(container_parts)]
        is_folder = len(parts) > len(container_parts) + 1
        applicable = [
            (filter_type, pattern)
            for filter_type, pattern in rules
            if (is_folder and filter_type & 8) or (not is_folder and filter_type & 4)
        ]
        include_only = [
            pattern for filter_type, pattern in applicable if filter_type & 1
        ]
        exclude_all = [
            pattern for filter_type, pattern in applicable if filter_type & 2
        ]
        if include_only and not any(
            fnmatch.fnmatchcase(child, pattern) for pattern in include_only
        ):
            return True
        if any(fnmatch.fnmatchcase(child, pattern) for pattern in exclude_all):
            return True
    return False


def sources(config: BuildConfig) -> list[Path]:
    exclusions = source_exclusions(config)
    filters = project_filters(config)
    result: list[Path] = []
    for path in (config.project_dir / "src").rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        relative = path.relative_to(config.project_dir)
        if is_source_excluded(relative, exclusions) or is_resource_filtered(
            relative, filters
        ):
            continue
        result.append(path)
    return sorted(result)


def tool(name: str) -> str:
    prefix = os.environ.get("CROSS_COMPILE", "arm-none-eabi-")
    executable = prefix + name
    if shutil.which(executable) is None:
        raise RuntimeError(f"{executable} was not found in PATH")
    return executable


def compile_source(
    config: BuildConfig, source: Path, c_flags: list[str], cxx_flags: list[str]
) -> Path:
    relative = source.relative_to(config.project_dir)
    obj = config.build_dir / "obj" / relative.with_suffix(relative.suffix + ".o")
    obj.parent.mkdir(parents=True, exist_ok=True)

    if source.suffix == ".c":
        command = [tool("gcc"), *c_flags]
    else:
        command = [tool("g++"), *cxx_flags]
    run([*command, "-c", str(source), "-o", str(obj)])
    return obj


def compile_project(config: BuildConfig) -> list[Path]:
    project_sources = sources(config)
    c_flags = compiler_flags(config, "c")
    cxx_flags = compiler_flags(config, "cxx")
    config.build_dir.mkdir(parents=True, exist_ok=True)
    print(
        f"Compiling {config.project}/{config.name}: {len(project_sources)} sources",
        flush=True,
    )

    with concurrent.futures.ThreadPoolExecutor(
        max_workers=os.cpu_count() or 1
    ) as executor:
        futures = [
            executor.submit(compile_source, config, source, c_flags, cxx_flags)
            for source in project_sources
        ]
        return [future.result() for future in futures]


def archive_project(config: BuildConfig) -> Path:
    objects = compile_project(config)
    archive = config.build_dir / f"lib{config.artifact_name}.a"
    run([tool("ar"), "rcs", str(archive), *map(str, objects)])
    print(f"Archived {archive.relative_to(ROOT)}", flush=True)
    return archive


def string_macros(config: BuildConfig) -> dict[str, str]:
    root = ET.parse(config.project_dir / ".cproject").getroot()
    config_id = config.config.get("id")
    for cconfiguration in root.iter("cconfiguration"):
        if any(
            child.get("id") == config_id
            for child in cconfiguration.iter("configuration")
        ):
            return {
                macro.get("name") or "": macro.get("value") or ""
                for macro in cconfiguration.iter("stringMacro")
            }
    return {}


def linker_flags(config: BuildConfig, map_file: Path) -> list[str]:
    values = scalar_values(config, "link.option.flags", "gnu.cpp.link")
    flags: list[str] = []
    for value in values:
        for flag in shlex.split(value):
            if flag.startswith("-Wl,-Map,"):
                continue
            if "${workspace_loc:/" in flag:
                start = flag.index("${workspace_loc:/")
                end = flag.rfind("}")
                workspace_path = flag[start + len("${workspace_loc:/") : end].replace(
                    "${ProjName}", config.project
                )
                flag = flag[:start] + str(ROOT / workspace_path) + flag[end + 1 :]
            flags.append(flag)
    flags.append(f"-Wl,-Map,{map_file}")
    return flags


def crc_appender() -> Path:
    system = platform.system()
    machine = platform.machine().lower()
    if machine not in {"x86_64", "amd64"}:
        raise RuntimeError(f"bundled CrcAppender does not support {machine}")
    if system == "Linux":
        path = ROOT / "RepRapFirmware/Tools/CrcAppender/linux-x86_64/CrcAppender"
    elif system == "Darwin":
        path = ROOT / "RepRapFirmware/Tools/CrcAppender/macos-x86_64/CrcAppender"
    else:
        raise RuntimeError(f"bundled CrcAppender is not supported on {system}")
    if not path.exists():
        raise RuntimeError(f"missing bundled CrcAppender: {path}")
    path.chmod(path.stat().st_mode | 0o111)
    return path


def build_firmware(archives: dict[str, Path]) -> Path:
    config = find_config(*FIRMWARE)
    objects = compile_project(config)
    elf = config.build_dir / f"{config.artifact_name}.elf"
    map_file = config.build_dir / f"{config.artifact_name}.map"
    binary = config.build_dir / f"{config.artifact_name}.bin"

    linked_libraries: list[str] = []
    for library in list_values(config, "link.option.libs", "gnu.cpp.link"):
        if library in archives:
            linked_libraries.append(str(archives[library]))
        else:
            linked_libraries.append(f"-l{library}")

    macros = string_macros(config)
    run(
        [
            tool("gcc"),
            *linker_flags(config, map_file),
            *shlex.split(macros.get("LinkFlags1", "")),
            *map(str, objects),
            *linked_libraries,
            *shlex.split(macros.get("LinkFlags2", "")),
            "-o",
            str(elf),
        ]
    )
    run([tool("objcopy"), "-O", "binary", str(elf), str(binary)])
    run([str(crc_appender()), str(binary)])
    run([tool("size"), str(elf)])
    print(f"Firmware: {binary.relative_to(ROOT)}", flush=True)
    return binary


def clean() -> None:
    shutil.rmtree(BUILD_ROOT, ignore_errors=True)


def build() -> None:
    clean()
    archives: dict[str, Path] = {}
    for project, name in LIBRARIES:
        config = find_config(project, name)
        archives[project] = archive_project(config)
    build_firmware(archives)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Native command-line builder for the SAM4E RepRapFirmware target"
    )
    parser.add_argument("command", choices=("build", "clean"))
    args = parser.parse_args()
    try:
        clean() if args.command == "clean" else build()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
