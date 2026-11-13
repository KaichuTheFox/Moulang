"""Compare successful Moulang checks on a generated multi-file source graph."""

import argparse
import csv
import os
from pathlib import Path
from statistics import median
import subprocess
import tempfile

from run import invoke


def fixture(root, module_count, functions_per_module, deep=False, warns=False):
    imports = []
    aliases = """
    int[] items = [1, 2]
    var alias = items
    int[] collected = []
    foreach (item in alias) {
        items[0] = item
        if (count > 1) { collected.append(item) }
    }
    print(collected)
""" if deep else ""
    if warns:
        aliases = aliases.replace('items[0] = item', 'items.append(item)')
    for module in range(module_count):
        name = f"module-{module}.mcx"
        imports.append(f'import "{name}"')
        # All modes use the same source graph within a workload.
        functions = [
            f"""int sample{module}_{index}(int count) {{
    str text = "Moulang"
    str result = ""
    foreach (part in text) {{
        str local = part + "!"
        print(local)
    }}
    for (i=0 in i++(textModule.len(text))) {{ print(text[i]) }}
    if (count > 1) {{ result = result + text }}
{aliases}    return textModule.len(result)
}}
"""
            for index in range(functions_per_module)
        ]
        (root / name).write_text('import "std:text" as textModule\n' + "\n".join(functions), encoding="utf-8")
    entry = root / "main.mcx"
    entry.write_text("\n".join(imports) + "\nmain() { print(sample0_0(2)) }\n", encoding="utf-8")
    return entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--deep", action="store_true", help="compare Deep on workloads with list aliases")
    options = parser.parse_args()
    if options.samples < 7:
        parser.error("use at least seven timed samples")
    before = options.before.resolve(strict=True)
    after = options.after.resolve(strict=True)
    workspace = Path("build/benchmarks").resolve()
    workspace.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="tiki-", dir=workspace))
    env = dict(os.environ, TEMP=str(work), TMP=str(work))
    modes = (("before", before, None), ("off", after, None), ("basic", after, "basic"),
             ("perf", after, "perf"), ("all", after, "all"))
    if options.deep:
        modes = modes[:3] + (("deep", after, "deep"),)
    rows = []
    workloads = [("small", 1, 5), ("multi_file", 20, 50)]
    if options.deep:
        workloads.append(("warning_emission", 1, 50))
    for workload, modules, functions in workloads:
        root = work / workload
        root.mkdir()
        warns = workload == "warning_emission"
        entry = fixture(root, modules, functions, deep=options.deep, warns=warns)
        expected = {mode: [] for mode, _, _ in modes}
        if warns:
            probe = subprocess.run([after, "check", entry, "--diagnostic-format=plain", "--tiki=deep"],
                                   cwd=root, env=env, capture_output=True, encoding="utf-8", check=True, timeout=90)
            warnings = probe.stderr.splitlines()
            if probe.stdout or len(warnings) != modules * functions or any(
                    ": warning TI-D001: " not in line for line in warnings):
                raise RuntimeError(f"Unexpected Deep warning output: {probe.stderr}")
            expected["deep"] = warnings
        runs = {mode: [] for mode, _, _ in modes}
        for sample in range(options.samples + 1):
            # Rotate order so the same mode does not always run first.
            offset = sample % len(modes)
            for mode, compiler, flag in modes[offset:] + modes[:offset]:
                command = [compiler, "check", entry, "--diagnostic-format=plain"]
                if flag:
                    command.append("--tiki=" + flag)
                output, elapsed, peak = invoke(command, root, env, measure_peak=True, expected_stderr=expected[mode])
                if output:
                    raise RuntimeError(f"Unexpected output from {mode}: {output}")
                if sample:
                    runs[mode].append((elapsed, peak))
        for mode, _, _ in modes:
            elapsed, peaks = zip(*runs[mode])
            rows.append({
                "workload": workload, "source_files": modules + 1, "functions": modules * functions + 1,
                "mode": mode, "median_ms": f"{median(elapsed):.3f}", "min_ms": f"{min(elapsed):.3f}",
                "max_ms": f"{max(elapsed):.3f}", "median_peak_bytes": int(median(peaks)), "samples": options.samples,
            })
    with options.output.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=tuple(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    for row in rows:
        print(f"{row['workload']} {row['mode']}: {row['median_ms']} ms")


if __name__ == "__main__":
    main()
