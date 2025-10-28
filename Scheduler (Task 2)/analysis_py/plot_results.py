import math
import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path
from collections import defaultdict

M_FIXED = 4
N_LIST = [100, 500, 1000, 10000]        # используется для отбора данных из summary
THREADS_SERIES = [0, 2, 4, 6, 12]       # 0 = seq baseline


def ensure_dirs(base_dir: Path):
    (base_dir / "figs").mkdir(parents=True, exist_ok=True)


def load_summary(base_dir: Path) -> pd.DataFrame:
    p = base_dir / "data" / "summary.csv"
    if not p.exists():
        raise FileNotFoundError(f"Не найден {p}. Сначала запусти run_experiments.py")
    df = pd.read_csv(p)

    # Нормализация типов
    df["threads"] = pd.to_numeric(df["threads"], errors="coerce").fillna(0).astype(int)
    df["M"] = pd.to_numeric(df["M"], errors="coerce")
    df["N"] = pd.to_numeric(df["N"], errors="coerce")
    df["avg_time_ms"] = pd.to_numeric(df["avg_time_ms"], errors="coerce")
    df["best_cost"] = pd.to_numeric(df["best_cost"], errors="coerce")

    return df


def compute_grouped(df_ok: pd.DataFrame) -> pd.DataFrame:
    """
    Агрегируем значения по (threads, N):
      - avg_time_ms: среднее по запускам
      - best_cost:   минимум (лучшая найденная стоимость)
    """
    grp = (
        df_ok
        .groupby(["threads", "N"], as_index=False)
        .agg(
            avg_time_ms=("avg_time_ms", "mean"),
            best_cost=("best_cost", "min")
        )
    )
    return grp


def add_quality_normalizations(grp: pd.DataFrame) -> pd.DataFrame:
    """
    Добавляет:
      - best_overall(N) = min_k best_cost(k,N)
      - excess_cost     = best_cost - best_overall(N)
      - rel_cost_pct    = 100 * best_cost / best_overall(N)
    """
    best_per_N = (
        grp
        .groupby("N", as_index=False)
        .agg(best_overall=("best_cost", "min"))
    )

    merged = pd.merge(grp, best_per_N, on="N", how="left")
    merged["excess_cost"] = merged["best_cost"] - merged["best_overall"]
    merged["rel_cost_pct"] = 100.0 * merged["best_cost"] / merged["best_overall"]
    return merged


def _get_sorted_measured_Ns(grp_norm: pd.DataFrame):
    """
    Возвращает отсортированный список значений N, которые реально есть в данных.
    """
    Ns_present = sorted(grp_norm["N"].unique())
    return Ns_present


def _plot_line_generic(
        grp_norm: pd.DataFrame,
        y_col: str,
        y_label: str,
        title: str,
        out_path: Path,
        yscale: str,
        custom_y=None
):
    """
    Вспомогательная функция для построения line-графиков времени и качества.

    Поведение:
      - Ось X: равномерно расположенные точки (индексы 0..K-1), а подписи — фактические N.
      - Ось Y: логарифмическая шкала (yscale='log') или иная, как задано.
      - Горизонтальная сетка только по основным делениям оси Y.
    """

    Ns_present = _get_sorted_measured_Ns(grp_norm)
    x_positions = list(range(len(Ns_present)))  # равномерные точки
    plt.figure(figsize=(8, 5))

    for thr in THREADS_SERIES:
        sub = grp_norm[(grp_norm["threads"] == thr) &
                       (grp_norm["N"].isin(Ns_present))]
        if sub.empty:
            continue

        # отсортируем по N, чтобы сопоставить с x_positions
        sub = sub.sort_values("N")

        # сопоставляем каждой N её индекс на оси X
        x_vals = [x_positions[Ns_present.index(n_val)] for n_val in sub["N"]]
        y_vals = sub[y_col].tolist()

        label = "seq" if thr == 0 else f"par-{thr}thr"
        plt.plot(x_vals, y_vals, marker="o", label=label)

    plt.xlabel("N (число работ)")
    plt.ylabel(y_label)
    plt.title(title)

    # масштаб по Y
    if yscale == "log":
        plt.yscale("log")
    elif yscale == "symlog":
        # внешний код для symlog обрабатывается в plot_line_cost_excess(),
        # здесь не используется
        pass

    # Сетка: только основные горизонтальные линии
    plt.grid(axis="y", which="major", linestyle="--", alpha=0.4)

    # Тики по X: равномерные позиции -> подписи реальными N
    plt.xticks(x_positions, [str(n) for n in Ns_present])

    # Кастомные настройки по Y, если переданы (например, лимиты или тики)
    if custom_y:
        if "yticks" in custom_y:
            plt.yticks(custom_y["yticks"], custom_y.get("yticklabels", None))
        if "ylim" in custom_y:
            plt.ylim(custom_y["ylim"][0], custom_y["ylim"][1])

    plt.legend()
    plt.tight_layout()
    plt.savefig(out_path, dpi=200, bbox_inches="tight")
    plt.close()


def plot_line_time(grp_norm: pd.DataFrame, base_dir: Path):
    """
    Время исполнения:
    - Y логарифмическая (экспоненциальная шкала).
    - X равномерный.
    """
    out_path = base_dir / "figs" / "line_time_cmp.png"
    _plot_line_generic(
        grp_norm=grp_norm,
        y_col="avg_time_ms",
        y_label="avg_time_ms (мс)",
        title=f"Сравнение скорости (M={M_FIXED})",
        out_path=out_path,
        yscale="log",
        custom_y=None
    )


def plot_line_cost_absolute(grp_norm: pd.DataFrame, base_dir: Path):
    """
    Абсолютное качество (сумма времен завершения):
    - Y логарифмическая.
    - X равномерный.
    """
    out_path = base_dir / "figs" / "line_cost_cmp.png"
    _plot_line_generic(
        grp_norm=grp_norm,
        y_col="best_cost",
        y_label="best_cost (сумма времен завершения)",
        title=f"Сравнение качества (абсолютные значения, M={M_FIXED}) — ниже лучше",
        out_path=out_path,
        yscale="log",
        custom_y=None
    )


def plot_line_cost_excess(grp_norm: pd.DataFrame, base_dir: Path):
    """
    excess_cost = best_cost - best_overall(N)

    Требования:
    - Ось X: равномерно расположенные точки (индексы), подписи — реальные N.
    - Ось Y: кастомная "квазилогарифмическая" шкала без отрицательных степеней.
      Мы вручную трансформируем значения excess_cost:

          transform(v) = 0,                если v <= 0
                         1 + log10(v),     иначе

      Тогда:
        v = 0      -> 0
        v = 1      -> 1
        v = 10     -> 2
        v = 100    -> 3
        ...
      То есть равномерный шаг по оси соответствует умножению на 10.

      Тики Y выставляются вручную:
        0          -> "0"
        1          -> "1"
        2          -> "1e1"
        3          -> "1e2"
        ...

    - Горизонтальная сетка только по основным делениям.
    """

    # Какие N реально присутствуют
    Ns_present = _get_sorted_measured_Ns(grp_norm)
    x_positions = list(range(len(Ns_present)))  # равномерные точки по X

    plt.figure(figsize=(8, 5))

    # Соберём все значения excess_cost, чтобы понять максимальный порядок
    all_excess_vals = []

    def transform_excess(v: float) -> float:
        if v <= 0:
            return 0.0
        return 1.0 + math.log10(v)

    # Рисуем линии
    for thr in THREADS_SERIES:
        sub = grp_norm[(grp_norm["threads"] == thr) &
                       (grp_norm["N"].isin(Ns_present))]
        if sub.empty:
            continue

        sub = sub.sort_values("N")
        x_vals = [x_positions[Ns_present.index(n_val)] for n_val in sub["N"]]

        y_orig = sub["excess_cost"].tolist()
        y_plot = [transform_excess(v) for v in y_orig]

        all_excess_vals.extend(y_orig)

        label = "seq" if thr == 0 else f"par-{thr}thr"
        plt.plot(x_vals, y_plot, marker="o", label=label)

    plt.xlabel("N (число работ)")
    plt.ylabel("excess_cost = best_cost - best_overall(N)")
    plt.title(f"Отрыв от лучшего решения (M={M_FIXED})")

    # Горизонтальная сетка только по основным делениям
    plt.grid(axis="y", which="major", linestyle="--", alpha=0.4)

    # X-ось: равномерные позиции, подписи фактические N
    plt.xticks(x_positions, [str(n) for n in Ns_present])

    # --- Кастомные тики по Y ---
    # Определяем максимальный порядок величины excess_cost
    positive_vals = [v for v in all_excess_vals if v is not None and v > 0]
    if positive_vals:
        max_excess = max(positive_vals)
    else:
        max_excess = 1.0

    if max_excess <= 1:
        max_pow = 0
    else:
        max_pow = math.ceil(math.log10(max_excess))

    # Точки на оси Y после трансформации:
    #   0 (это реальный 0)
    #   1 + 0 = 1   (это 1)
    #   1 + 1 = 2   (это 10)
    #   1 + 2 = 3   (это 100)
    # и т.д.
    yticks_vals = [0.0] + [1.0 + p for p in range(0, max_pow + 1)]

    yticks_labels = []
    for p_idx, tick_val in enumerate(yticks_vals):
        if tick_val == 0.0:
            yticks_labels.append("0")
        else:
            # tick_val = 1 + p  <=> исходное значение ~10^p
            p = int(round(tick_val - 1.0))
            if p == 0:
                yticks_labels.append("1")
            else:
                yticks_labels.append(f"1e{p}")  # "1e1", "1e2", ...

    plt.yticks(yticks_vals, yticks_labels)

    # Немного поджать верхнюю границу
    ylim_top = (1.0 + max_pow) + 0.2
    plt.ylim(0.0, ylim_top)

    plt.legend()
    plt.tight_layout()
    out_path = base_dir / "figs" / "line_cost_excess_cmp.png"
    plt.savefig(out_path, dpi=200, bbox_inches="tight")
    plt.close()



def plot_bar_cost_relative(grp_norm: pd.DataFrame, base_dir: Path):
    """
    rel_cost_pct = 100 * best_cost / best_overall(N).
    100% = лучшее найденное решение при данном N.
    Шкала Y ограничена [100%, max*1.05].
    Горизонтальная сетка только по основным делениям.
    """
    rel_map = defaultdict(dict)
    for _, row in grp_norm.iterrows():
        N = int(row["N"])
        thr = int(row["threads"])
        rel_map[N][thr] = float(row["rel_cost_pct"])

    Ns_present = sorted(rel_map.keys())
    width = 0.15
    x = range(len(Ns_present))

    all_vals = []
    for N in Ns_present:
        for thr in THREADS_SERIES:
            if thr in rel_map[N]:
                all_vals.append(rel_map[N][thr])

    max_val = max(all_vals) if all_vals else 100.0
    ymax = max_val
    ymin = 100.0

    plt.figure(figsize=(10, 5))
    for i, thr in enumerate(THREADS_SERIES):
        xs = [xi + (i - (len(THREADS_SERIES)-1)/2.0)*width for xi in x]
        ys = []
        for N in Ns_present:
            ys.append(rel_map[N].get(thr, None))
        label = "seq" if thr == 0 else f"par-{thr}thr"
        plt.bar(xs, ys, width=width, label=label)

    plt.axhline(100.0, color="gray", linestyle="--", linewidth=1)

    plt.xticks(list(x), [str(n) for n in Ns_present])
    plt.xlabel("N (число работ)")
    plt.ylabel("rel_cost_pct = 100 * best_cost / best_overall(N)")
    plt.title(f"Качество относительно лучшего найденного (M={M_FIXED}) — 100% лучшее")

    plt.grid(axis="y", which="major", linestyle="--", alpha=0.4)

    plt.legend()
    plt.ylim(ymin, ymax)

    out_path = base_dir / "figs" / "bar_cost_rel_cmp.png"
    plt.tight_layout()
    plt.savefig(out_path, dpi=200, bbox_inches="tight")
    plt.close()


def bar_gain_plots(df_ok: pd.DataFrame, base_dir: Path):
    """
    bar_speed_gain.png, bar_quality_gain.png

    speed_gain_k(N)   = 100 * T_seq / T_par
    quality_gain_k(N) = 100 * C_seq / C_par

    Для bar_quality_gain шкала Y = [100%, max*1.05].
    Сетка: только основные горизонтальные линии.
    """
    grp = (
        df_ok
        .groupby(["threads", "N"], as_index=False)
        .agg(
            avg_time_ms=("avg_time_ms", "mean"),
            best_cost=("best_cost", "min")
        )
    )

    time_by_thrN = defaultdict(dict)
    cost_by_thrN = defaultdict(dict)

    for _, row in grp.iterrows():
        thr = int(row["threads"])
        N = int(row["N"])
        time_by_thrN[N][thr] = float(row["avg_time_ms"])
        cost_by_thrN[N][thr] = float(row["best_cost"])

    Ns_present = sorted(time_by_thrN.keys())
    series_thr = [2, 4, 6, 12]
    width = 0.18
    x = range(len(Ns_present))

    # --- bar_speed_gain.png ---
    plt.figure(figsize=(10, 5))
    for i, thr in enumerate(series_thr):
        xs = [xi + (i - 1.5)*width for xi in x]
        ys = []
        for N in Ns_present:
            t_seq = time_by_thrN[N].get(0, None)
            t_par = time_by_thrN[N].get(thr, None)
            if t_seq is None or t_par is None or t_par <= 0:
                ys.append(0.0)
            else:
                ys.append(100.0 * t_seq / t_par)
        plt.bar(xs, ys, width=width, label=f"par-{thr}thr")

    plt.axhline(100.0, color="gray", linestyle="--", linewidth=1)
    plt.xticks(list(x), [str(n) for n in Ns_present])
    plt.xlabel("N (число работ)")
    plt.ylabel("Speed gain, % = 100 * T_seq / T_par")
    plt.title(f"Процентный относительный выигрыш по времени (M={M_FIXED})")

    plt.grid(axis="y", which="major", linestyle="--", alpha=0.4)

    plt.legend()
    out_path = base_dir / "figs" / "bar_speed_gain.png"
    plt.tight_layout()
    plt.savefig(out_path, dpi=200, bbox_inches="tight")
    plt.close()

    # --- bar_quality_gain.png ---
    bars_data = []
    all_vals_q = []

    for i, thr in enumerate(series_thr):
        xs = [xi + (i - 1.5)*width for xi in x]
        ys = []
        for N in Ns_present:
            c_seq = cost_by_thrN[N].get(0, None)
            c_par = cost_by_thrN[N].get(thr, None)
            if c_seq is None or c_par is None or c_par <= 0:
                val = 0.0
            else:
                val = 100.0 * c_seq / c_par
            ys.append(val)
            all_vals_q.append(val)
        bars_data.append((xs, ys, f"par-{thr}thr"))

    max_val_q = max(all_vals_q) if all_vals_q else 100.0
    ymax_q = max_val_q
    ymin_q = 100.0

    plt.figure(figsize=(10, 5))
    for xs, ys, label in bars_data:
        plt.bar(xs, ys, width=width, label=label)

    plt.axhline(100.0, color="gray", linestyle="--", linewidth=1)
    plt.xticks(list(x), [str(n) for n in Ns_present])
    plt.xlabel("N (число работ)")
    plt.ylabel("Quality gain, % = 100 * C_seq / C_par")
    plt.title(f"Процентный относительный прирост качества к seq (M={M_FIXED}) — выше 100% лучше")

    plt.grid(axis="y", which="major", linestyle="--", alpha=0.4)

    plt.legend()
    plt.ylim(ymin_q, ymax_q)

    out_path = base_dir / "figs" / "bar_quality_gain.png"
    plt.tight_layout()
    plt.savefig(out_path, dpi=200, bbox_inches="tight")
    plt.close()


def main():
    base_dir = Path(__file__).resolve().parent
    ensure_dirs(base_dir)

    df = load_summary(base_dir)

    # используем только валидные строки, нужные M и интересующие нас N_LIST;
    # далее фактические Ns_present получаем уже из данных
    df_ok = df[
        (df["error"].isna()) &
        (df["M"] == M_FIXED) &
        (df["N"].isin(N_LIST)) &
        (df["avg_time_ms"].notna()) &
        (df["best_cost"].notna())
        ].copy()

    grp = compute_grouped(df_ok)
    grp_norm = add_quality_normalizations(grp)

    # Линейные графики (равномерный X, кастомный Y)
    plot_line_time(grp_norm, base_dir)
    plot_line_cost_absolute(grp_norm, base_dir)
    plot_line_cost_excess(grp_norm, base_dir)

    # Гистограмма относительного качества к лучшему
    plot_bar_cost_relative(grp_norm, base_dir)

    # Гистограммы выигрыша относительно seq
    bar_gain_plots(df_ok, base_dir)

    print("Сохранены графики в analysis_py/figs/:")
    print("  - line_time_cmp.png")
    print("  - line_cost_cmp.png")
    print("  - line_cost_excess_cmp.png")
    print("  - bar_cost_rel_cmp.png")
    print("  - bar_speed_gain.png")
    print("  - bar_quality_gain.png")


if __name__ == "__main__":
    main()
