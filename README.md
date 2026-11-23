# Parallel Search Engine with LLM Query Expansion (C++20 & Web UI)

> **Курсова робота з дисципліни:** «Паралельні обчислення»  
> **Спеціальність:** 122 Комп’ютерні науки | НН ІПСА, КПІ ім. Ігоря Сікорського

Високопродуктивна розподілена пошукова система на **C++20** з власною реалізацією багатопотокового пулу потоків (**ThreadPool**), потокобезпечним інвертованим індексом (**InvertedIndex** на основі BM25), інтеграцією з локальною нейромережею **Ollama** для розширення запитів (Query Expansion), підтримкою **REST API**, **WebSocket стрімінгу**, автономним веб-інтерфейсом (**Web UI**) та **CLI-клієнтом**.

---

## 🚀 Основні можливості

- **Паралельна побудова індексу:** Багатопотокова обробка текстових файлів через власний `ThreadPool` з регулюванням кількості потоків (параметр `threads` у `POST /api/index`).
- **Потокобезпечна структура даних:** Шардований інвертований індекс (64 незалежні шарди з `std::shared_mutex`), що унеможливлює стан гонки (Race Condition) та взаємні блокування (Deadlock).
- **BM25 Ранжування:** Алгоритм оцінки релевантності BM25 із виділенням контекстних сніпетів у тексті.
- **LLM Query Expansion (Ollama):** Автоматичне семантичне розширення запитів синонімами через `POST /api/generate`.
- **WebSocket Streaming:** Стрімінг етапів виконання запиту в реальному часі (`query_expansion` $\to$ `keyword_search` $\to$ `done`).
- **Автономний Web UI:** Візуалізація стрімінгу етапів пайплайну, керування індексацією в реальному часі без зайвих залежностей.
- **CLI Клієнт:** Консольний застосунок на C++ для швидкого пошуку та індексації з терміналу.
- **Docker Compose:** Повний запуск системи однією командою `docker compose up`.

---

## 📦 Швидкий старт (Docker Compose)

### 1. Запуск всієї системи
```bash
docker compose up --build
```

Після запуску:
- **Web UI:** відкрийте у браузері [http://localhost:8088](http://localhost:8088)
- **REST & WebSocket API:** доступні на `http://localhost:8088`
- **Ollama LLM:** доступна на `http://localhost:11434` (модель завантажується автоматично в `./models/ollama`)

- **GPU-прискорення:** конфігурація `docker-compose.yml` містить резервацію NVIDIA GPU (`driver: nvidia, capabilities: [gpu]`).
- **Локальність:** всі моделі та дані зберігаються всередині однієї папки проекту (`./models/ollama` та `./data/documents`).

---

## 💻 Використання CLI-клієнта

Здійснення пошуку:
```bash
# Базовий пошук за ключовими словами
./search "parallel computing algorithms"

# Пошук із розширенням через LLM
./search -m llm "artificial intelligence neural network"

# Перебудова індексу з використанням 8 потоків
./search -i 8

# Перегляд статистики індексу
./search --stats
```

---

## 📡 Специфікація API

### 1. Health Check
`GET /api/health`
```json
{
  "status": "ok",
  "ollama": true
}
```

### 2. Побудова індексу (Паралельно)
`POST /api/index`
```json
// Request
{
  "threads": 8
}

// Response
{
  "status": "ok",
  "documents_indexed": 12500,
  "time_ms": 710
}
```

### 3. Пошук за ключовими словами
`POST /api/search`
```json
// Request
{
  "query": "space telescope",
  "mode": "keyword"
}

// Response
{
  "results": [
    {
      "doc_id": "doc_42.txt",
      "score": 0.96,
      "snippet": "...the Hubble space telescope observed distant galaxies in..."
    }
  ],
  "time_ms": 14,
  "expanded_terms": ["space", "telescope"]
}
```

### 4. Статистика індексу
`GET /api/stats`
```json
{
  "documents": 12500,
  "index_build_time_ms": 710,
  "threads_used": 8
}
```

### 5. WebSocket Стрімінг
`WS /ws/search`

Клієнт надсилає:
```json
{ "query": "space telescope" }
```

Сервер послідовно передає:
1. `{"stage": "query_expansion", "data": {"original": "...", "expanded_terms": ["astronomy", "hubble", "galaxy"]}}`
2. `{"stage": "keyword_search", "data": {"results": [...], "time_ms": 12}}`
3. `{"stage": "done"}`

---

## 📊 Дослідження масштабованості (Scalability Benchmark)

Для автоматизованого зняття показників та дослідження закону Амдала запустіть:
```bash
python scripts/benchmark.py
```
Результати зберігаються у файлі `benchmark_results.json` та містять:
- Час побудови індексу залежно від кількості потоків $T(p)$ ($p \in [1, 2, 4, 8, 12, 16]$).
- Прискорення $S(p) = \frac{T(1)}{T(p)}$ та ефективність $E(p) = \frac{S(p)}{p} \times 100\%$.
- Пропускну здатність Throughput (запитів/сек) під навантаженням одночасних клієнтів.

---

## 📐 UML-Діаграми

### 1. Use Case Diagram
![Use Case Diagram](graphics/png/use_case.png)

### 2. Class Diagram
![Class Diagram](graphics/png/class_diagram.png)

### 3. Sequence Diagram
![Sequence Diagram](graphics/png/sequence_diagram.png)

### 4. Deployment Diagram
![Deployment Diagram](graphics/png/deployment_diagram.png)

Вихідні файли PlantUML розміщені в папці `docs/uml/`:
- `use_case.puml`
- `class_diagram.puml`
- `sequence_diagram.puml`
- `deployment_diagram.puml`

---

## 📚 Матеріали до захисту курсової роботи

У файлі [`docs/theoretical_answers.md`](docs/theoretical_answers.md) підготовлено детальний конспект відповідей на теоретичні питання для захисту:
- Закон Амдала та межі прискорення паралельних обчислень.
- Проблема стану гонки (Race Condition) та взаємних блокувань (Deadlock).
- Шардування структур даних та вибір гранулярності блокувань (`std::shared_mutex`).
- Принцип роботи ранжування BM25 та його переваги над класичним TF-IDF.
- Архітектура черги задач у пулі потоків (ThreadPool).
