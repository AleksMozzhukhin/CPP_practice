import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

# Эти значения должны совпадать с run_heatmap_experiments.py
M_FIXED = 4
THREADS_LIST = list(range(1, 13))
N_LIST = list(range(100, 5001, 250))


def ensure_dirs(base_dir: Path):
    (base_dir / "figs").mkdir(parents=True, exist_ok=True)


def load_heatmap_df(base_dir: Path) -> pd.DataFrame:
    p = base_dir / "data" / "summary_heatmap.csv"
    if not p.exists():
        raise FileNotFoundError(f"Не найден {p}. Сначала запусти run_heatmap_experiments.py")
    df = pd.read_csv(p)

    # Нормализация типов и отбор без ошибок
    df["threads"] = pd.to_numeric(df["threads"], errors="coerce")
    df["N"] = pd.to_numeric(df["N"], errors="coerce")
    df["avg_time_ms"] = pd.to_numeric(df["avg_time_ms"], errors="coerce")
    df["best_cost"] = pd.to_numeric(df["best_cost"], errors="coerce")

    df_ok = df[df["error"].isna()].copy()
    return df_ok


def build_matrix(df_ok: pd.DataFrame, value_col: str) -> np.ndarray:
    """
    Строит матрицу Z размером [len(THREADS_LIST), len(N_LIST)],
    где Z[i,j] = агрегированное значение (среднее/мин) для threads=THREADS_LIST[i], N=N_LIST[j].
    Если данных нет — ставим NaN.

    Для нашего случая в summary_heatmap.csv каждая пара (thread, N) уже одна.
    Но на всякий случай возьмём mean/min так же, как делали раньше.
    """
    # готовим словарь {(thr,N)->value}
    agg = (df_ok
    .groupby(["threads", "N"], as_index=False)
    .agg({
        "avg_time_ms": "mean",
        "best_cost": "min"
    }))

    # создадим матрицу
    Z = np.full((len(THREADS_LIST), len(N_LIST)), np.nan, dtype=float)

    for _, row in agg.iterrows():
        thr = int(row["threads"])
        N = int(row["N"])
        if thr in THREADS_LIST and N in N_LIST:
            i = THREADS_LIST.index(thr)
            j = N_LIST.index(N)
            if value_col == "avg_time_ms":
                Z[i, j] = float(row["avg_time_ms"])
            elif value_col == "best_cost":
                Z[i, j] = float(row["best_cost"])
            else:
                raise ValueError(f"unknown value_col {value_col}")
    return Z


def plot_heatmap(Z: np.ndarray,
                 x_labels,
                 y_labels,
                 title: str,
                 cbar_label: str,
                 out_path: Path):
    """
    Рисует тепловую карту (imshow) для матрицы Z.
    Ось X -> N, Ось Y -> threads.
    """
    plt.figure(figsize=(10, 5))
    # aspect='auto' чтобы не растягивать квадраты до одинаковой стороны насильно.
    im = plt.imshow(Z,
                    aspect='auto',
                    origin='lower',  # чтобы threads=1 был внизу графика
                    interpolation='nearest')
    plt.colorbar(im, label=cbar_label)

    # подписи осей
    plt.xticks(ticks=range(len(x_labels)), labels=[str(x) for x in x_labels], rotation=45, ha="right")
    plt.yticks(ticks=range(len(y_labels)), labels=[str(y) for y in y_labels])

    plt.xlabel("N (число задач)")
    plt.ylabel("threads (число потоков)")
    plt.title(title)
    plt.tight_layout()
    plt.savefig(out_path, dpi=200, bbox_inches="tight")
    plt.close()


def main():
    base_dir = Path(__file__).resolve().parent
    ensure_dirs(base_dir)

    df_ok = load_heatmap_df(base_dir)

    # Матрица времени
    Z_time = build_matrix(df_ok, "avg_time_ms")
    # Матрица качества
    Z_cost = build_matrix(df_ok, "best_cost")

    out_time = base_dir / "figs" / "heatmap_time.png"
    out_cost = base_dir / "figs" / "heatmap_quality.png"

    plot_heatmap(
        Z_time,
        x_labels=N_LIST,
        y_labels=THREADS_LIST,
        title=f"Тепловая карта времени (M={M_FIXED})",
        cbar_label="avg_time_ms (мс)",
        out_path=out_time
    )

    plot_heatmap(
        Z_cost,
        x_labels=N_LIST,
        y_labels=THREADS_LIST,
        title=f"Тепловая карта качества (M={M_FIXED}) — меньше лучше",
        cbar_label="best_cost",
        out_path=out_cost
    )

    print("Сохранены тепловые карты:")
    print(" ", out_time)
    print(" ", out_cost)


if __name__ == "__main__":
    main()
