#!/usr/bin/env python3
"""
Скрипт автоматизованого тестування та бенчмаркінгу для курсової роботи:
1. Дослідження масштабованості побудови індексу залежно від кількості потоків (1..16 потоків).
2. Розрахунок прискорення S(p) = T(1) / T(p) та ефективності E(p) = S(p) / p.
3. Дослідження пропускної здатності Throughput (запитів/сек) залежно від кількості одночасних клієнтів.
4. Збереження результатів у CSV та JSON для звіту курсової.
"""

import sys
import time
import json
import urllib.request
import urllib.error
import concurrent.futures
from typing import List, Dict, Any

# Ensure UTF-8 output on Windows
if sys.stdout.encoding != 'utf-8':
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass

import os

SERVER_URL = os.getenv("SERVER_URL", "http://localhost:8088")


def check_server():
    try:
        req = urllib.request.urlopen(f"{SERVER_URL}/api/health", timeout=3)
        if req.status == 200:
            print("[OK] Server is online and responding.")
            return True
    except Exception as e:
        print(f"[FAIL] Cannot connect to server at {SERVER_URL}: {e}")
        return False
    return False

def benchmark_indexing(threads_list: List[int]) -> List[Dict[str, Any]]:
    print("\n" + "="*60)
    print("  1. ТЕСТУВАННЯ ЧАСУ ПОБУДОВИ ІНДЕКСУ (Threads vs Time)")
    print("="*60)

    results = []
    t1_time = None

    for threads in threads_list:
        payload = json.dumps({"threads": threads}).encode('utf-8')
        req = urllib.request.Request(
            f"{SERVER_URL}/api/index",
            data=payload,
            headers={"Content-Type": "application/json"}
        )

        try:
            start_t = time.time()
            with urllib.request.urlopen(req, timeout=120) as resp:
                resp_data = json.loads(resp.read().decode('utf-8'))
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
                "efficiency": efficiency
            }
            results.append(entry)

            print(f"  Потоків: {threads:2d} | Час: {elapsed_ms:5d} ms | Прискорення S(p): {speedup:4.2f}x | Ефективність E(p): {efficiency:5.1f}%")
        except Exception as e:
            print(f"  [!] Error on {threads} threads: {e}")

    return results

def send_search_request(query: str = "parallel computing"):
    payload = json.dumps({"query": query, "mode": "keyword"}).encode('utf-8')
    req = urllib.request.Request(
        f"{SERVER_URL}/api/search",
        data=payload,
        headers={"Content-Type": "application/json"}
    )
    start_t = time.time()
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            resp.read()
            return time.time() - start_t
    except Exception:
        return None

def benchmark_throughput(client_concurrencies: List[int], total_requests: int = 100) -> List[Dict[str, Any]]:
    print("\n" + "="*60)
    print("  2. ТЕСТУВАННЯ ПРОПУСКНОЇ ЗДАТНОСТІ (Throughput vs Clients)")
    print("="*60)

    results = []

    for clients in client_concurrencies:
        start_time = time.time()
        successful_requests = 0
        latencies = []

        with concurrent.futures.ThreadPoolExecutor(max_workers=clients) as executor:
            futures = [executor.submit(send_search_request) for _ in range(total_requests)]
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
            "avg_latency_ms": avg_latency_ms
        }
        results.append(entry)

        print(f"  Клієнтів: {clients:2d} | Throughput: {rps:6.1f} req/s | Сер. затримка: {avg_latency_ms:6.2f} ms")

    return results

def main():
    print("Запуск автоматизованого тестування пошукового сервера...")
    if not check_server():
        print("Будь ласка, запустіть пошуковий сервер перед тестуванням:")
        print("  docker compose up  АБО  ./build/server/search_server")
        return

    # 1. Бенчмарк індексації
    indexing_results = benchmark_indexing([1, 2, 4, 8, 12, 16])

    # 2. Бенчмарк Throughput
    throughput_results = benchmark_throughput([1, 5, 10, 25, 50], total_requests=100)

    # Збереження результатів у JSON
    summary = {
        "indexing_scalability": indexing_results,
        "search_throughput": throughput_results
    }

    with open("benchmark_results.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)

    print("\n[OK] Результати збережено у benchmark_results.json для оформлення звіту!")

if __name__ == "__main__":
    main()
