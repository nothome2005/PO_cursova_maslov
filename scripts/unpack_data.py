#!/usr/bin/env python3
import os
import zipfile
import sys

# Ensure UTF-8 output on Windows
if sys.stdout.encoding != 'utf-8':
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass

def unpack():
    zip_path = "D1.7GB.zip"
    dest_dir = os.path.join("data", "documents")
    os.makedirs(dest_dir, exist_ok=True)

    if not os.path.exists(zip_path):
        print(f"[!] File {zip_path} not found. Creating sample documents in {dest_dir}...")
        create_samples(dest_dir)
        return

    print(f"[*] Unpacking {zip_path} into {dest_dir}...")
    try:
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(dest_dir)
        print(f"[OK] Successfully unpacked into {dest_dir}")
    except Exception as e:
        print(f"[!] Error unpacking zip: {e}")
        print("[*] Creating fallback sample documents...")
        create_samples(dest_dir)

def create_samples(dest_dir):
    samples = {
        "parallel_computing_intro.txt": "Parallel computing is a type of computation in which many calculations or processes are carried out simultaneously. Large problems can often be divided into smaller ones, which can then be solved at the same time using multiple processor threads.",
        "inverted_index_data_structure.txt": "An inverted index is a database index storing a mapping from content, such as words or numbers, to its locations in a document or a set of documents. The purpose of an inverted index is to allow fast full-text searches.",
        "large_language_models.txt": "Large language models (LLMs) and artificial intelligence algorithms are capable of expanding search queries with contextual synonyms, understanding semantic relationships between concepts, and generating human-like responses.",
        "operating_systems_concurrency.txt": "In operating systems, multithreading and thread synchronization mechanisms such as mutexes, semaphores, and condition variables prevent race conditions and deadlocks in concurrent execution environments."
    }
    for fname, text in samples.items():
        with open(os.path.join(dest_dir, fname), "w", encoding="utf-8") as f:
            f.write(text)
    print(f"[OK] Created sample documents in {dest_dir}")

if __name__ == "__main__":
    unpack()
