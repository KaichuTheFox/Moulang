"""Measure and verify the Windows baseline workloads."""

import argparse
import csv
import ctypes
from ctypes import wintypes
import math
import os
from pathlib import Path
import platform
from statistics import median
import subprocess
import sys
import tempfile
from time import perf_counter
import winreg

import baseline


FIXTURES = Path(__file__).resolve().parent
LANGUAGES = ("Moulang", "C++20", "CPython")
STRING_FOREACH = ("ascii_foreach", "hangul_foreach")
STRING_INDEX = {
    "ascii_index_short": 50000, "ascii_index_long": 8000,
    "hangul_index_short": 50000, "hangul_index_long": 8000,
}
INT_LIST = {
    "int_slice_append_read": 511370976,
    "int_list_append_read": 511370976,
    "int_list_write": 8128,
    "int_list_view_write": 8128,
}
INTEGER_CASTS = {
    "runtime_integer_casts": 372000000,
    "bounded_integer_casts": 372000000,
}
LIST_TRAVERSAL = {
    "list_indexed": "int index = 0\n        while (index < length) {\n"
                    "            if (values[index] == \"a!\") { matches++ }\n"
                    "            index++\n        }",
    "list_foreach": "foreach (value in values) { if (value == \"a!\") { matches++ } }",
}
EXPECTED = {
    "integer_math": baseline.integer_math(),
    "real_math": baseline.real_math(),
    "loop_count": 76924,
    "function_calls": baseline.function_calls(),
    "string_concat": True,
    "list_access": 50004,
    "list_sort": 50000,
    "ascii_foreach": 1_200_000,
    "hangul_foreach": 1_200_000,
    "ascii_indexed": 4000,
    "hangul_indexed": 4000,
    "file_io": True,
}


class ProcessMemoryCounters(ctypes.Structure):
    _fields_ = [("size", wintypes.DWORD), ("page_faults", wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            "peak_working_set", "working_set", "peak_paged_pool", "paged_pool",
            "peak_nonpaged_pool", "nonpaged_pool", "pagefile", "peak_pagefile"
        )
    ]


get_memory = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
get_memory.argtypes = (wintypes.HANDLE, ctypes.POINTER(ProcessMemoryCounters), wintypes.DWORD)
get_memory.restype = wintypes.BOOL


def invoke(command, directory, environment, measure_peak=False, expected_stderr=None):
    started = perf_counter()
    with subprocess.Popen(command, cwd=directory, env=environment,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          encoding="utf-8", errors="replace") as process:
        try:
            output, errors = process.communicate(timeout=90)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
        milliseconds = (perf_counter() - started) * 1000
        if process.returncode:
            raise RuntimeError(f"{Path(command[0]).name} exited {process.returncode}\n{errors}")
        if expected_stderr is not None and errors.splitlines() != expected_stderr:
            raise RuntimeError(f"Unexpected diagnostics from {Path(command[0]).name}: {errors}")
        if measure_peak:
            counters = ProcessMemoryCounters()
            counters.size = ctypes.sizeof(counters)
            # CPython keeps the Windows process handle open after communicate().
            if not get_memory(int(process._handle), ctypes.byref(counters), counters.size):
                raise ctypes.WinError(ctypes.get_last_error())
            if not counters.peak_working_set:
                raise RuntimeError("Windows did not report a process peak")
            return output.strip(), milliseconds, counters.peak_working_set
    return output.strip(), milliseconds, None


def verify(actual, expected):
    if isinstance(expected, bool):
        valid = actual.casefold() == str(expected).casefold()
    elif isinstance(expected, float):
        valid = math.isclose(float(actual), expected, rel_tol=1e-14, abs_tol=1e-14)
    else:
        valid = actual == str(expected)
    if not valid:
        raise ValueError(f"Expected {expected}, got {actual!r}")


def prepare(case, language, folder, options):
    binary = folder / "bench.exe"
    if language == "Moulang":
        source = folder / "bench.m"
        if case in LIST_TRAVERSAL:
            template = (FIXTURES / "list-traversal.m").read_text(encoding="utf-8")
            program = template.replace("// traversal", LIST_TRAVERSAL[case])
        else:
            fixture = ("string-index.m" if options.string_index else
                       "int-list.m" if options.int_lists else
                       "integer-casts.m" if options.integer_casts else
                       "string-foreach.m" if options.string_foreach else "baseline.m")
            program = (FIXTURES / fixture).read_text(encoding="utf-8") + "\nmain() { print(" + case + "()) }\n"
        source.write_text(program, encoding="utf-8")
        return [options.moulang, "build", source, "-o", binary, options.moulang_opt], [binary]
    if language == "C++20":
        source = folder / "bench.cpp"
        source.write_text((FIXTURES / "baseline.cpp").read_text(encoding="utf-8") +
                          "\nint main() { std::cout << std::boolalpha << std::setprecision(17) << "
                          + case + "() << '\\n'; }\n", encoding="utf-8")
        return [options.cxx, "-std=c++20", "-O2", "-static", source, "-o", binary], [binary]
    source = folder / "bench.py"
    source.write_text((FIXTURES / "baseline.py").read_text(encoding="utf-8") +
                      "\nprint(" + case + "())\n", encoding="utf-8")
    return None, [sys.executable, "-B", source]


def measure(case, language, options, root, environment, metadata):
    row = {"workload": case, "language": language, **metadata[language],
           "comparison": "same-workload", "compile_ms": "N/A", "execute_ms": "N/A",
           "peak_working_set_bytes": "N/A", "exe_bytes": "N/A", "samples": 0}
    if case == "list_sort":
        row["comparison"] = "builtin-sort-only"
    if case.endswith("_indexed"):
        row["comparison"] = "indexed-sweep-only"
    if (language == "Moulang" and case in STRING_FOREACH and not options.string_foreach or
            language != "Moulang" and case.endswith("_indexed")):
        row["comparison"] = "N/A"
        return row

    folder = root / case / language
    folder.mkdir(parents=True)
    compile_command, command = prepare(case, language, folder, options)
    compile_times = []
    runtime_times = []
    peaks = []
    if case in LIST_TRAVERSAL:
        expected = 4096000
    elif options.int_lists:
        expected = INT_LIST[case]
    elif options.integer_casts:
        expected = INTEGER_CASTS[case]
    else:
        expected = (STRING_INDEX if options.string_index else EXPECTED)[case]
    for sample in range(options.samples + 1):
        run_dir = folder / f"run-{sample}"
        run_dir.mkdir()
        if compile_command:
            _, compile_time, _ = invoke(compile_command, folder, environment)
        output, runtime, peak = invoke(command, run_dir, environment, measure_peak=True)
        verify(output, expected)
        if sample == 0:
            continue
        if compile_command:
            compile_times.append(compile_time)
        runtime_times.append(runtime)
        peaks.append(peak)

    if compile_command:
        row.update(compile_ms=f"{median(compile_times):.3f}", exe_bytes=command[0].stat().st_size)
    row.update(execute_ms=f"{median(runtime_times):.3f}",
               peak_working_set_bytes=int(median(peaks)), samples=options.samples)
    return row


def save(rows, output, cpu):
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=tuple(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    versions = {r["language"]: (r["version"], r["flags"]) for r in rows}
    lines = ["# Moulang benchmark results", "",
             f"- Hardware is {cpu}.", f"- OS is Windows {platform.version()} ({platform.machine()})."]
    lines += [f"- {name} uses {version} with {flags}." for name, (version, flags) in versions.items()]
    lines += [
        "", f"Reported values are medians of {max(row['samples'] for row in rows)} measured runs after one warm-up.",
        "Execution includes process startup, computation and a single captured result. Peak working set is the process's Windows-reported resident memory high-water mark.",
        "Native compile times include compiler launch and linking. CPython runs source with -B, so native compile time and EXE size are N/A.",
    ]
    if all(row["workload"] in STRING_FOREACH for row in rows):
        lines.append("Each sweep compares 4000 code points across 300 passes and verifies 1200000 matches.")
    elif all(row["workload"] in STRING_INDEX for row in rows):
        lines.append("Each iteration checks a positive and a negative index. Short strings have 16 code points and long strings have 16384.")
    elif all(row["workload"] in LIST_TRAVERSAL for row in rows):
        lines.append("Each list traversal verifies 4096000 comparisons across 1000 passes.")
    elif all(row["workload"] in INT_LIST for row in rows):
        lines.append("The append/read cases append and read 1,000,000 values. The write cases fill a list of 262,144 elements and perform 128 writes, with or without a new pinned view per write.")
    elif all(row["workload"] in INTEGER_CASTS for row in rows):
        lines.append("Each case performs 12,000,000 iterations with two integer narrowing casts. The runtime case reads values through the list runtime. The bounded case derives values from a bit mask in the loop.")
    else:
        lines.extend([
            "List sort rows time each implementation's built-in sorting. The indexed sweep is a separate Moulang workload.",
            "The historical baseline reports Moulang string foreach as N/A. Use --string-foreach with a compiler that supports it.",
            "File I/O uses fresh directories on the same host. File caches may affect the results.",
        ])
    lines += [
        "", "| Workload | Language | Compile ms | Execute ms | Peak bytes | EXE bytes |",
        "| --- | --- | ---: | ---: | ---: | ---: |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(str(row[key]) for key in (
            "workload", "language", "compile_ms", "execute_ms", "peak_working_set_bytes", "exe_bytes")) + " |")
    output.with_suffix(".md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--moulang", type=Path, required=True)
    parser.add_argument("--cxx", type=Path, required=True, help="Pinned LLVM-MinGW clang++.exe")
    parser.add_argument("--moulang-opt", choices=("-O0", "-O2", "-O3"), default="-O2")
    parser.add_argument("--samples", type=int, default=5)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--string-foreach", action="store_true", help="Run only the ASCII and Hangul foreach cases")
    mode.add_argument("--string-index", action="store_true", help="Run only the Moulang ASCII and Hangul index cases")
    mode.add_argument("--list-traversal", choices=tuple(LIST_TRAVERSAL), help="Run a Moulang list traversal case")
    mode.add_argument("--int-lists", action="store_true", help="Compare legacy integer slices and shared integer lists")
    mode.add_argument("--integer-casts", action="store_true", help="Measure runtime and bounded integer narrowing")
    parser.add_argument("--output", type=Path, required=True, help="CSV file with a Markdown report beside it")
    options = parser.parse_args()
    if options.samples < 3 or options.samples % 2 == 0 or options.output.suffix != ".csv":
        parser.error("use an odd sample count of at least three and a .csv output")
    options.moulang = options.moulang.resolve(strict=True)
    options.cxx = options.cxx.resolve(strict=True)
    build = FIXTURES.parents[1] / "build" / "benchmarks"
    build.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="baseline-", dir=build))
    environment = dict(os.environ, TEMP=str(root), TMP=str(root))
    moulang_version, _, _ = invoke([options.moulang, "--version"], root, environment)
    moulang_only = options.string_index or options.list_traversal or options.int_lists or options.integer_casts
    cxx_version = "" if moulang_only else invoke([options.cxx, "--version"], root, environment)[0]
    metadata = {
        "Moulang": {"version": moulang_version, "flags": options.moulang_opt},
        "C++20": {"version": cxx_version.split(" (", 1)[0].split("\n", 1)[0], "flags": "-std=c++20 -O2 -static"},
        "CPython": {"version": platform.python_version(), "flags": "-B"},
    }
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
        cpu = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
    rows = []
    cases = EXPECTED
    if options.string_index:
        cases = STRING_INDEX
    elif options.string_foreach:
        cases = STRING_FOREACH
    elif options.list_traversal:
        cases = (options.list_traversal,)
    elif options.int_lists:
        cases = INT_LIST
    elif options.integer_casts:
        cases = INTEGER_CASTS
    languages = ("Moulang",) if moulang_only else LANGUAGES
    for case in cases:
        for language in languages:
            row = measure(case, language, options, root, environment, metadata)
            rows.append(row)
            print(f"{case} {language}: {row['execute_ms']}", flush=True)
    save(rows, options.output, cpu)


if __name__ == "__main__":
    main()
