#!/usr/bin/env python3
"""
Скрипт автоматичного багатопотокового завантаження датасету DeepMind Gutenberg
із публічного Google Cloud Storage бакета (gs://deepmind-gutenberg) у папку data/documents/.
"""

import os
import sys
import json
import urllib.request
import concurrent.futures
import argparse

# Ensure UTF-8 output on Windows
if sys.stdout.encoding != 'utf-8':
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass

GCS_API_URL = "https://storage.googleapis.com/storage/v1/b/deepmind-gutenberg/o"
GCS_DOWNLOAD_URL = "https://storage.googleapis.com/deepmind-gutenberg/"

def list_files(max_files=2000, prefix=""):
    print(f"[*] Отримання списку файлів з Google Cloud Storage (prefix='{prefix}')...")
    files = []
    page_token = None

    while len(files) < max_files:
        url = f"{GCS_API_URL}?maxResults=1000"
        if prefix:
            url += f"&prefix={prefix}"
        if page_token:
            url += f"&pageToken={page_token}"

        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode('utf-8'))
            items = data.get('items', [])
            for item in items:
                name = item.get('name', '')
                if name.endswith('.txt'):
                    files.append(name)
                    if len(files) >= max_files:
                        break

            page_token = data.get('nextPageToken')
            if not page_token or not items:
                break

    print(f"[✓] Знайдено {len(files)} текстових файлів у бакеті.")
    return files

def download_single_file(filename, dest_dir):
    url = GCS_DOWNLOAD_URL + filename
    # Використовуємо плоску назву для уникнення проблем з вкладеними шляхами
    safe_name = filename.replace('/', '_')
    dest_path = os.path.join(dest_dir, safe_name)

    if os.path.exists(dest_path) and os.path.getsize(dest_path) > 0:
        return True

    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req, timeout=30) as resp:
            content = resp.read()
            with open(dest_path, 'wb') as f:
                f.write(content)
        return True
    except Exception as e:
        print(f"[!] Помилка завантаження {filename}: {e}")
        return False

def download_dataset(limit=2000, max_threads=16, dest_dir="data/documents"):
    os.makedirs(dest_dir, exist_ok=True)
    files = list_files(max_files=limit)

    print(f"[*] Завантаження {len(files)} файлів у {dest_dir} за допомогою {max_threads} потоків...")
    success_count = 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=max_threads) as executor:
        futures = {executor.submit(download_single_file, f, dest_dir): f for f in files}
        for i, future in enumerate(concurrent.futures.as_completed(futures)):
            if future.result():
                success_count += 1
            if (i + 1) % 100 == 0 or (i + 1) == len(files):
                print(f"  Завантажено: {i + 1}/{len(files)} файлів ({(i + 1)/len(files)*100:.1f}%)")

    print(f"\n[✓] Успішно завантажено {success_count} книг у папку {dest_dir}!")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Завантажувач датасету DeepMind Gutenberg")
    parser.add_argument("--limit", type=int, default=2000, help="Кількість файлів для завантаження (default: 2000)")
    parser.add_argument("--threads", type=int, default=16, help="Кількість паралельних потоків (default: 16)")
    parser.add_argument("--dest", type=str, default="data/documents", help="Папка для збереження")
    args = parser.parse_args()

    download_dataset(limit=args.limit, max_threads=args.threads, dest_dir=args.dest)
