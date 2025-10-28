import subprocess
import pandas as pd
from pathlib import Path
import time


# ------------------------------
# Константы для теплокарты
# ------------------------------

M_FIXED = 4

# Ось X: N от 100 до 5000 включительно с шагом 250, начиная с 100
N_LIST = list(range(100, 5001, 250))
# Проверка на последнюю точку: 100 + 250*k <= 5000
# k=0..19 -> последняя будет 100 + 19*250 = 4850. Всё нормально.

# Ось Y: количество потоков (заметим, это не "M", это реально threads в параллельном SA)
THREADS_LIST = list(range(1, 13))  # [1,2,...,12]

# Фиксированные параметры SA
COOLING_TYPE = "geom"
COOLING_T0 = 3000
COOLING_PARAM = 0.995  # alpha

MAX_NO_IMPROVE = 200
OUTER_NO_IMPROVE = 5
RUNS = 1  # один прогон на конфигурацию для ускорения

# Лимит итераций: чуть растёт с N
def hard_limit_for(N: int) -> int:
    """
    Эмпирически: чем больше N, тем выше бюджет.
    Можно линейно накинуть, чтобы не было слишком слабого поиска.
    """
    if N <= 500:
        return 200_000
    elif N <= 2000:
        return 400_000
    else:
        return 800_000


def seed_for(N: int) -> int:
    """
    Сид детерминируем только от N.
    Так все варианты threads для одного N будут решать тот же инстанс.
    """
    return 900000 + N


def ensure_dirs(base_dir: Path):
    (base_dir / "data").mkdir(parents=True, exist_ok=True)
    (base_dir / "figs").mkdir(parents=True, exist_ok=True)


def build_command(build_dir: Path,
                  M: int,
                  N: int,
                  threads: int,
                  seed: int,
                  hard_limit: int,
                  out_csv_path: Path):
    """
    Сформировать команду запуска research для одной пары (threads, N)
    """
    research_bin = build_dir / "research"
    cmd = [
        str(research_bin),
        "--mode", "par",
        "--M-list", str(M),
        "--N-list", str(N),
        "--p-min", "1",
        "--p-max", "50",
        "--runs", str(RUNS),
        "--cooling", COOLING_TYPE, str(COOLING_T0), str(COOLING_PARAM),
        "--max-no-improve", str(MAX_NO_IMPROVE),
        "--hard-limit", str(hard_limit),
        "--csv", str(out_csv_path),
        "--seed", str(seed),
        "--threads", str(threads),
        "--outer-no-improve", str(OUTER_NO_IMPROVE),
    ]
    return cmd


def run_single(build_dir: Path,
               base_dir: Path,
               M: int,
               N: int,
               threads: int) -> pd.DataFrame:
    """
    Запускает один эксперимент (threads, N).
    Возвращает DataFrame одной строки с колонками:
        threads, N, avg_time_ms, best_cost
    или с error, если не получилось.
    """
    seed = seed_for(N)
    hard_limit = hard_limit_for(N)

    exp_name = f"heat_thr{threads}_N{N}"
    out_csv = base_dir / "data" / f"{exp_name}.csv"
    out_log = base_dir / "data" / f"{exp_name}.log"

    cmd = build_command(build_dir, M, N, threads, seed, hard_limit, out_csv)

    print(f"[run] {exp_name}")
    t0 = time.time()
    res = subprocess.run(
        cmd,
        cwd=str(build_dir),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    t1 = time.time()

    with out_log.open("w", encoding="utf-8") as f:
        f.write("COMMAND:\n" + " ".join(cmd) + "\n\n")
        f.write("STDOUT:\n" + res.stdout + "\n")
        f.write("STDERR:\n" + res.stderr + "\n")
        f.write(f"RUNTIME_SEC: {t1 - t0}\n")

    if res.returncode != 0 or not out_csv.exists():
        err = f"rc={res.returncode}, csv_exists={out_csv.exists()}"
        print(f"[run] {exp_name}: ERROR {err}")
        return pd.DataFrame([{
            "threads": threads,
            "N": N,
            "avg_time_ms": None,
            "best_cost": None,
            "error": err,
        }])

    # research вывел CSV формата: M,N,avg_time_ms,best_cost
    df_local = pd.read_csv(out_csv)
    # Ожидаем ровно одну строку, но если нет — усредним вручную
    # (например, runs=1 -> одна строка)
    df_local["threads"] = threads
    df_local["error"] = None

    # Оставим нужные столбцы: threads, N, avg_time_ms, best_cost, error
    cols = ["threads", "N", "avg_time_ms", "best_cost", "error"]
    # Найдём среднее/минимум на случай, если csv вдруг несёт несколько N (не должен)
    df_out = (df_local
    .groupby(["threads", "N"], as_index=False)
    .agg({
        "avg_time_ms": "mean",
        "best_cost": "min",
        "error": "min"
    }))
    # Убедимся, что есть все нужные колонки
    for c in cols:
        if c not in df_out.columns:
            df_out[c] = None
    return df_out[cols]


def main():
    base_dir = Path(__file__).resolve().parent
    build_dir = (base_dir / ".." / "build").resolve()

    ensure_dirs(base_dir)

    frames = []

    for threads in THREADS_LIST:
        for N in N_LIST:
            df_entry = run_single(build_dir, base_dir, M_FIXED, N, threads)
            frames.append(df_entry)

    df_all = pd.concat(frames, ignore_index=True)

    # Сохраняем общий сводный файл
    summary_path = base_dir / "data" / "summary_heatmap.csv"
    df_all.to_csv(summary_path, index=False)
    print(f"[done] Heatmap summary saved to {summary_path}")


if __name__ == "__main__":
    main()
