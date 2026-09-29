#!/usr/bin/env bash

set -euo pipefail

project_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$project_directory"

python_executable="$project_directory/venv/bin/python"
if [[ ! -x "$python_executable" ]]; then
    python_executable="python3"
fi

mkdir -p results
cmake -S . -B build
cmake --build build -j
./build/bench 2>&1 | tee results/benchmark.log
"$python_executable" scripts/plot_results.py results

echo "Готово: результаты, лог и графики находятся в $project_directory/results"
