#!/usr/bin/env python3

import csv
import math
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter


COLLECTOR_ORDER = [
    "BaselineCollector",
    "DryRunMutexCollector",
    "MutexCollector",
    "LockStripingCollector",
    "ThreadLocalCollector",
    "DoubleBufferCollector",
    "BrokenDoubleBufferCollector",
]

COLLECTOR_LABELS = {
    "BaselineCollector": "Baseline",
    "DryRunMutexCollector": "Пустой mutex",
    "MutexCollector": "Общий mutex",
    "LockStripingCollector": "16 шардов",
    "ThreadLocalCollector": "Thread-local",
    "DoubleBufferCollector": "Двойная буферизация",
    "BrokenDoubleBufferCollector": "Двойная буферизация без шага 3",
}

COLORS = {
    "BaselineCollector": "#94a3b8",
    "DryRunMutexCollector": "#f59e0b",
    "MutexCollector": "#ef4444",
    "LockStripingCollector": "#8b5cf6",
    "ThreadLocalCollector": "#10b981",
    "DoubleBufferCollector": "#2563eb",
    "BrokenDoubleBufferCollector": "#0f172a",
}


plt.rcParams.update(
    {
        "font.family": "DejaVu Sans",
        "font.size": 10,
        "axes.titlesize": 14,
        "axes.labelsize": 11,
        "axes.edgecolor": "#cbd5e1",
        "axes.linewidth": 0.8,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "xtick.color": "#475569",
        "ytick.color": "#475569",
        "text.color": "#0f172a",
        "axes.labelcolor": "#334155",
        "savefig.facecolor": "white",
    }
)


def read_csv(path):
    if not path.exists():
        raise SystemExit(f"Нет файла {path}. Сначала запустите run_benchmarks.sh")
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def save_figure(figure, output):
    figure.savefig(output.with_suffix(".svg"), bbox_inches="tight")
    figure.savefig(output.with_suffix(".png"), dpi=180, bbox_inches="tight")
    plt.close(figure)


def write_throughput(rows, output):
    series = defaultdict(list)
    for row in rows:
        series[row["collector"]].append((int(row["threads"]), float(row["median_mops"])))
    if not series:
        raise SystemExit("benchmark.csv не содержит результатов")

    figure, axis = plt.subplots(figsize=(8.4, 4.8))
    names = [name for name in COLLECTOR_ORDER if name in series]
    names.extend(sorted(set(series) - set(names)))

    for name in names:
        points = sorted(series[name])
        threads = [thread for thread, _ in points]
        values = [value for _, value in points]
        if name == "BaselineCollector":
            axis.axhline(
                values[0],
                color=COLORS[name],
                linestyle="--",
                linewidth=1.8,
                label=COLLECTOR_LABELS[name],
            )
            axis.scatter(threads, values, color=COLORS[name], s=20, zorder=3)
            continue
        axis.plot(
            threads,
            values,
            color=COLORS.get(name, "#334155"),
            marker="o",
            markersize=4,
            linewidth=1.8,
            label=COLLECTOR_LABELS.get(name, name),
        )

    axis.set_title("Пропускная способность", loc="left", pad=14)
    axis.set_xlabel("Число потоков")
    axis.set_ylabel("Млн операций/с (лог. шкала)")
    axis.set_xscale("log", base=2)
    axis.set_yscale("log")
    axis.set_xticks([1, 2, 4, 8, 16])
    axis.xaxis.set_major_formatter(ScalarFormatter())
    axis.minorticks_off()
    axis.grid(axis="y", which="major", color="#e2e8f0", linewidth=0.8)
    axis.legend(frameon=False, ncol=2, loc="best", handlelength=2.4)
    figure.tight_layout()
    save_figure(figure, output)


def write_histogram(rows, output):
    values = [(int(row["value"]), int(row["count"])) for row in rows]
    if not values:
        raise SystemExit("input_histogram.csv не содержит данных")

    bin_count = 64
    largest_value = max(value for value, _ in values)
    bin_width = math.ceil(largest_value / bin_count)
    bins = [0] * bin_count
    for value, count in values:
        index = min((value - 1) // bin_width, bin_count - 1)
        bins[index] += count
    centers = [index * bin_width + bin_width / 2 for index in range(bin_count)]

    figure, axis = plt.subplots(figsize=(8.4, 4.8))
    axis.bar(centers, bins, width=bin_width * 0.88, color="#2563eb", linewidth=0)
    axis.set_title("Распределение входных значений", loc="left", pad=14)
    axis.set_xlabel("Значение")
    axis.set_ylabel("Количество (лог. шкала)")
    axis.set_xlim(0, largest_value + 1)
    axis.set_yscale("log")
    axis.grid(axis="y", which="major", color="#e2e8f0", linewidth=0.8)
    axis.set_axisbelow(True)
    figure.tight_layout()
    save_figure(figure, output)


def write_summary(benchmark_rows, stress_rows, output):
    benchmark = defaultdict(dict)
    for row in benchmark_rows:
        benchmark[row["collector"]][int(row["threads"])] = float(row["median_mops"])

    lines = [
        "# Результаты замеров",
        "",
        "| Реализация | 1 поток | 2 потока | 4 потока | 8 потоков | 16 потоков |",
        "| :--- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for name in COLLECTOR_ORDER:
        if name not in benchmark:
            continue
        cells = []
        for thread in [1, 2, 4, 8, 16]:
            value = benchmark[name].get(thread)
            cells.append(f"{value:.3f}" if value is not None else "—")
        lines.append(f"| {COLLECTOR_LABELS[name]} | " + " | ".join(cells) + " |")

    lines.extend(
        [
            "",
            "![Сводный график](throughput.svg)",
            "",
            "![Гистограмма входных данных](input_histogram.svg)",
            "",
            "## Стресс-тест",
            "",
            "| Реализация | Битых снимков | Меньше | Равно | Больше | Разница итогового count |",
            "| :--- | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for row in stress_rows:
        name = row["collector"]
        lines.append(
            f'| {COLLECTOR_LABELS.get(name, name)} | {float(row["broken_percent"]):.2f}% | '
            f'{row["less"]} | {row["equal"]} | {row["greater"]} | {row["difference"]} |'
        )
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    results_directory = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("results")
    benchmark_rows = read_csv(results_directory / "benchmark.csv")
    histogram_rows = read_csv(results_directory / "input_histogram.csv")
    stress_rows = read_csv(results_directory / "stress.csv")

    write_throughput(benchmark_rows, results_directory / "throughput")
    write_histogram(histogram_rows, results_directory / "input_histogram")
    write_summary(benchmark_rows, stress_rows, results_directory / "results.md")
    print(f"Графики и сводка сохранены в {results_directory}")


if __name__ == "__main__":
    main()
