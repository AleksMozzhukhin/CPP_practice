import subprocess
import pandas as pd
from pathlib import Path
import time


# ------------------------------
# Константы серии экспериментов
# ------------------------------

M_FIXED = 4
N_LIST = [100, 500, 1000, 10000]

# Сколько повторов усреднять для каждого N
RUNS_BY_N = {
    100: 5,
    500: 3,
    1000: 2,
    10000: 1
}

# Один и тот же закон охлаждения для всех вариантов (seq/par)
COOLING_TYPE = "geom"    # geom | cauchy | linear
COOLING_T0 = 3000
COOLING_PARAM = 0.995    # alpha (для geom)

# Лимиты SA (одинаковые по всем вариантам)
MAX_NO_IMPROVE = 200
# Для больших N не жалко дать больше пространства итераций
HARD_LIMIT_BY_N = {
    100:   200_000,
    500:   400_000,
    1000:  600_000,
    10000: 1_200_000,
}

# Для par: волна без улучшения (внешняя стагнация)
OUTER_NO_IMPROVE = 5

# Набор потоков для сравнения
THREADS_LIST = [2, 4, 6, 12]

# Сиды по N (одинаковый сид для seq и всех par-вариантов на этом N)
SEED_BY_N = {
    100:   4242100,
    500:   4242500,
    1000:  4243000,
    10000: 4249999,
}


def ensure_dirs(base_dir: Path):
    (base_dir / "data").mkdir(parents=True, exist_ok=True)
    (base_dir / "figs").mkdir(parents=True, exist_ok=True)


def build_research_cmd(build_dir: Path, cfg: dict, out_csv: Path):
    """
    Сформировать команду запуска ../build/research по конфигурации cfg.
    cfg содержит всё нужное: mode, M_list, N_list, runs, cooling, лимиты, seed, threads?
    """
    research_bin = build_dir / "research"
    cmd = [
        str(research_bin),
        "--mode", cfg["mode"],
        "--M-list", ",".join(map(str, cfg["M_list"])),
        "--N-list", ",".join(map(str, cfg["N_list"])),
        "--p-min", "1",
        "--p-max", "50",
        "--runs", str(cfg["runs"]),
        "--cooling", COOLING_TYPE, str(COOLING_T0), str(COOLING_PARAM),
        "--max-no-improve", str(MAX_NO_IMPROVE),
        "--hard-limit", str(cfg["hard_limit"]),
        "--csv", str(out_csv),
        "--seed", str(cfg["seed"]),
    ]
    if cfg["mode"] == "par":
        cmd += ["--threads", str(cfg["threads"]),
                "--outer-no-improve", str(OUTER_NO_IMPROVE)]
    return cmd


def run_one(build_dir: Path, base_dir: Path, cfg: dict) -> pd.DataFrame:
    """
    Запускает один эксперимент research (cfg) и возвращает DataFrame результатов
    (с добавленными метаданными). Если ошибка — одна строка с полем error != None.
    """
    exp_name = cfg["name"]
    out_csv = base_dir / "data" / f"{exp_name}.csv"
    out_log = base_dir / "data" / f"{exp_name}.log"

    cmd = build_research_cmd(build_dir, cfg, out_csv)

    print(f"[run] {exp_name}")
    print("      ", " ".join(cmd))

    t0 = time.time()
    res = subprocess.run(
        cmd, cwd=str(build_dir),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
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
            "mode": cfg["mode"],
            "threads": cfg.get("threads", None),
            "M": None, "N": None,
            "avg_time_ms": None, "best_cost": None,
            "runs": cfg["runs"],
            "seed": cfg["seed"],
            "exp_name": exp_name,
            "error": err,
        }])

    df = pd.read_csv(out_csv)  # ожидается: M,N,avg_time_ms,best_cost
    df["mode"] = cfg["mode"]
    df["threads"] = cfg.get("threads", None)
    df["runs"] = cfg["runs"]
    df["seed"] = cfg["seed"]
    df["exp_name"] = exp_name
    df["error"] = None
    return df


def main():
    base_dir = Path(__file__).resolve().parent
    build_dir = (base_dir / ".." / "build").resolve()

    ensure_dirs(base_dir)

    experiments = []

    # Для каждого N создаём 1 seq-эксперимент и 4 par-эксперимента
    for N in N_LIST:
        seed = SEED_BY_N[N]
        runs = RUNS_BY_N[N]
        hard_limit = HARD_LIMIT_BY_N[N]

        # seq baseline
        experiments.append({
            "name": f"seq_M{M_FIXED}_N{N}",
            "mode": "seq",
            "M_list": [M_FIXED],
            "N_list": [N],
            "runs": runs,
            "seed": seed,
            "hard_limit": hard_limit
        })

        # par with threads ∈ {2,4,6,12}
        for thr in THREADS_LIST:
            experiments.append({
                "name": f"par{thr}_M{M_FIXED}_N{N}",
                "mode": "par",
                "M_list": [M_FIXED],
                "N_list": [N],
                "runs": runs,
                "seed": seed,             # тот же сид, чтобы инстанс был тот же!
                "threads": thr,
                "hard_limit": hard_limit
            })

    dfs = []
    for cfg in experiments:
        dfs.append(run_one(build_dir, base_dir, cfg))

    df_all = pd.concat(dfs, ignore_index=True)
    # Нормализуем threads: для seq проставим 0
    df_all["threads"] = df_all["threads"].fillna(0).astype(int)

    summary = base_dir / "data" / "summary.csv"
    df_all.to_csv(summary, index=False)
    print(f"[done] Saved {summary}")


if __name__ == "__main__":
    main()
