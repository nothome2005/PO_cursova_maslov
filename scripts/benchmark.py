#!/usr/bin/env python3
"""
Скрипт автоматизованого тестування та бенчмаркінгу для курсової роботи:
1. Дослідження масштабованості побудови індексу залежно від кількості потоків (1..16 потоків).
2. Розрахунок прискорення S(p) = T(1) / T(p) та ефективності E(p) = S(p) / p.
3. Дослідження пропускної здатності Throughput (запитів/сек) залежно від кількості одночасних клієнтів.
4. Перевірка тестових пошукових запитів (keyword + llm) та збереження результатів у JSON і PNG-графіки.
"""

import os
import sys
import time
import json
import urllib.request
import urllib.error
import concurrent.futures
from typing import List, Dict, Any

if sys.stdout.encoding != 'utf-8':
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass

SERVER_URL = os.getenv("SERVER_URL", "http://localhost:8088")


def check_server() -> Dict[str, Any] | None:
    try:
        with urllib.request.urlopen(f"{SERVER_URL}/api/health", timeout=5) as req:
            if req.status == 200:
                data = json.loads(req.read().decode("utf-8"))
                print(f"[OK] Server is online: {data}")
                return data
    except Exception as e:
        print(f"[FAIL] Cannot connect to server at {SERVER_URL}: {e}")
    return None


def benchmark_indexing(threads_list: List[int]) -> List[Dict[str, Any]]:
    print("\n" + "=" * 68)
    print("  1. ТЕСТУВАННЯ ЧАСУ ПОБУДОВИ ІНДЕКСУ (Threads vs Time)")
    print("=" * 68)

    results = []
    t1_time = None

    for threads in threads_list:
        payload = json.dumps({"threads": threads}).encode("utf-8")
        req = urllib.request.Request(
            f"{SERVER_URL}/api/index",
            data=payload,
            headers={"Content-Type": "application/json"},
        )

        try:
            start_t = time.time()
            with urllib.request.urlopen(req, timeout=900) as resp:
                resp_data = json.loads(resp.read().decode("utf-8"))
                elapsed_ms = resp_data.get("time_ms", int((time.time() - start_t) * 1000))
                docs = resp_data.get("documents_indexed", 0)

            if threads == 1 or t1_time is None:
                t1_time = elapsed_ms

            speedup = round(t1_time / elapsed_ms, 2) if elapsed_ms > 0 else 1.0
            efficiency = round((speedup / threads) * 100, 1)

            entry = {
                "threads": threads,
                "time_ms": elapsed_ms,
                "documents": docs,
                "speedup": speedup,
                "efficiency": efficiency,
            }
            results.append(entry)

            print(
                f"  Потоків: {threads:2d} | Документів: {docs} | "
                f"Час: {elapsed_ms:6d} ms | S(p): {speedup:4.2f}x | E(p): {efficiency:5.1f}%"
            )
        except Exception as e:
            print(f"  [!] Error on {threads} threads: {e}")

    return results


def send_search_request(query: str = "parallel computing"):
    payload = json.dumps({"query": query, "mode": "keyword"}).encode("utf-8")
    req = urllib.request.Request(
        f"{SERVER_URL}/api/search",
        data=payload,
        headers={"Content-Type": "application/json"},
    )
    start_t = time.time()
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            resp.read()
            return time.time() - start_t
    except Exception:
        return None


def benchmark_throughput(client_concurrencies: List[int], total_requests: int = 100) -> List[Dict[str, Any]]:
    print("\n" + "=" * 68)
    print("  2. ТЕСТУВАННЯ ПРОПУСКНОЇ ЗДАТНОСТІ (Throughput vs Clients)")
    print("=" * 68)

    queries = [
        "space astronomy stars",
        "french revolution history",
        "philosophy logic mind",
        "ocean voyage ship",
        "mathematics geometry proof",
    ]
    results = []

    for clients in client_concurrencies:
        start_time = time.time()
        successful_requests = 0
        latencies = []

        with concurrent.futures.ThreadPoolExecutor(max_workers=clients) as executor:
            futures = [
                executor.submit(send_search_request, queries[i % len(queries)])
                for i in range(total_requests)
            ]
            for fut in concurrent.futures.as_completed(futures):
                res = fut.result()
                if res is not None:
                    successful_requests += 1
                    latencies.append(res)

        total_duration = time.time() - start_time
        rps = round(successful_requests / total_duration, 1) if total_duration > 0 else 0
        avg_latency_ms = round((sum(latencies) / len(latencies)) * 1000, 2) if latencies else 0

        entry = {
            "concurrent_clients": clients,
            "total_requests": total_requests,
            "successful_requests": successful_requests,
            "duration_sec": round(total_duration, 2),
            "throughput_rps": rps,
            "avg_latency_ms": avg_latency_ms,
        }
        results.append(entry)

        print(
            f"  Клієнтів: {clients:2d} | Успішно: {successful_requests}/{total_requests} | "
            f"Throughput: {rps:6.1f} req/s | Сер. затримка: {avg_latency_ms:6.2f} ms"
        )

    return results


def run_test_queries() -> List[Dict[str, Any]]:
    print("\n" + "=" * 68)
    print("  3. ТЕСТОВІ ПОШУКОВІ ЗАПИТИ (Keyword та LLM Query Expansion)")
    print("=" * 68)

    cases = [
        {"query": "parallel computing", "mode": "keyword"},
        {"query": "astronomy telescope planets", "mode": "keyword"},
        {"query": "shakespeare hamlet tragedy", "mode": "keyword"},
        {"query": "ocean voyage ship", "mode": "llm"},
        {"query": "space telescope stars", "mode": "llm"},
    ]
    rows = []

    for case in cases:
        payload = json.dumps(case).encode("utf-8")
        req = urllib.request.Request(
            f"{SERVER_URL}/api/search",
            data=payload,
            headers={"Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                results = data.get("results", [])
                top = results[0] if results else None
                row = {
                    "query": case["query"],
                    "mode": case["mode"],
                    "expanded_terms": data.get("expanded_terms", []),
                    "count": len(results),
                    "time_ms": data.get("time_ms", 0),
                    "top_doc": top["doc_id"] if top else None,
                    "top_score": top["score"] if top else 0.0,
                }
                rows.append(row)
                print(
                    f"  [{case['mode']:7s}] \"{case['query']}\" -> {len(results)} рез., "
                    f"top={row['top_doc']} ({row['top_score']}), {row['time_ms']} ms, "
                    f"terms={row['expanded_terms']}"
                )
        except Exception as e:
            print(f"  [!] Query '{case['query']}' failed: {e}")

    return rows


def generate_plots(indexing_results: List[Dict[str, Any]], throughput_results: List[Dict[str, Any]]):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("[!] matplotlib не встановлено — графіки пропущено.")
        return

    os.makedirs(os.path.join("graphics", "png"), exist_ok=True)

    if indexing_results:
        threads = [r["threads"] for r in indexing_results]
        times_s = [r["time_ms"] / 1000.0 for r in indexing_results]
        speedups = [r["speedup"] for r in indexing_results]
        efficiencies = [r["efficiency"] for r in indexing_results]

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.8), dpi=160)

        ax1.plot(threads, times_s, marker="o", linewidth=2.2, color="#0284c7", label="Час побудови T(p)")
        for x, y in zip(threads, times_s):
            ax1.annotate(f"{y:.1f} с", (x, y), textcoords="offset points", xytext=(0, 8), ha="center", fontsize=8.5)
        ax1.set_title("Час побудови індексу залежно від потоків", fontsize=11, fontweight="bold")
        ax1.set_xlabel("Кількість потоків ThreadPool (p)")
        ax1.set_ylabel("Час індексації (сек)")
        ax1.set_xticks(threads)
        ax1.grid(True, linestyle="--", alpha=0.45)
        ax1.legend()

        ax2.plot(threads, speedups, marker="s", linewidth=2.2, color="#16a34a", label="Фактичне прискорення S(p)")
        ax2.plot(threads, threads, linestyle="--", linewidth=1.3, color="#94a3b8", label="Ідеальне лінійне (p)")
        for x, s, e in zip(threads, speedups, efficiencies):
            ax2.annotate(f"{s:.2f}x\n({e:.0f}%)", (x, s), textcoords="offset points", xytext=(0, 6), ha="center", fontsize=8)
        ax2.set_title("Прискорення S(p) та ефективність E(p)", fontsize=11, fontweight="bold")
        ax2.set_xlabel("Кількість потоків ThreadPool (p)")
        ax2.set_ylabel("Прискорення S(p) = T(1)/T(p)")
        ax2.set_xticks(threads)
        ax2.grid(True, linestyle="--", alpha=0.45)
        ax2.legend()

        plt.tight_layout()
        out_idx = os.path.join("graphics", "png", "benchmark_indexing.png")
        plt.savefig(out_idx)
        plt.close(fig)
        print(f"[OK] Графік збережено: {out_idx}")

    if throughput_results:
        clients = [r["concurrent_clients"] for r in throughput_results]
        rps = [r["throughput_rps"] for r in throughput_results]
        lat = [r["avg_latency_ms"] for r in throughput_results]

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.8), dpi=160)

        ax1.plot(clients, rps, marker="o", linewidth=2.2, color="#7c3aed", label="Throughput (req/s)")
        for x, y in zip(clients, rps):
            ax1.annotate(f"{y:.0f}", (x, y), textcoords="offset points", xytext=(0, 8), ha="center", fontsize=8.5)
        ax1.set_title("Пропускна здатність vs Одночасні клієнти", fontsize=11, fontweight="bold")
        ax1.set_xlabel("Кількість одночасних клієнтів")
        ax1.set_ylabel("Запитів за секунду (req/s)")
        ax1.set_xticks(clients)
        ax1.grid(True, linestyle="--", alpha=0.45)
        ax1.legend()

        ax2.plot(clients, lat, marker="^", linewidth=2.2, color="#ea580c", label="Середня затримка (мс)")
        for x, y in zip(clients, lat):
            ax2.annotate(f"{y:.1f} мс", (x, y), textcoords="offset points", xytext=(0, 8), ha="center", fontsize=8.5)
        ax2.set_title("Середня затримка відповіді vs Клієнти", fontsize=11, fontweight="bold")
        ax2.set_xlabel("Кількість одночасних клієнтів")
        ax2.set_ylabel("Середній час відповіді (мс)")
        ax2.set_xticks(clients)
        ax2.grid(True, linestyle="--", alpha=0.45)
        ax2.legend()

        plt.tight_layout()
        out_tp = os.path.join("graphics", "png", "benchmark_throughput.png")
        plt.savefig(out_tp)
        plt.close(fig)
        print(f"[OK] Графік збережено: {out_tp}")


def main():
    print("Запуск автоматизованого тестування пошукового сервера...")
    if not check_server():
        print("Будь ласка, запустіть пошуковий сервер перед тестуванням:")
        print("  docker compose up  АБО  ./build/server/search_server")
        return

    indexing_results = benchmark_indexing([1, 2, 4, 8, 12, 16])
    throughput_results = benchmark_throughput([1, 5, 10, 25, 50], total_requests=100)
    query_results = run_test_queries()

    summary = {
        "indexing_scalability": indexing_results,
        "search_throughput": throughput_results,
        "test_queries": query_results,
    }

    with open("benchmark_results.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)

    generate_plots(indexing_results, throughput_results)
    print("\n[OK] Результати збережено у benchmark_results.json та graphics/png/!")


if __name__ == "__main__":
    main()
