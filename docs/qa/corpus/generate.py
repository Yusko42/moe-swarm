#!/usr/bin/env python3
"""Детерминированный синтетический QA-корпус. Стандартная библиотека Python.

Без сервера: создаёт стартовые тексты, помечает tokenization_status=UNCALIBRATED.
С --server-url: калибрует длины через /apply-template + /tokenize данного llama-server.
В обоих случаях оператор ОБЯЗАН перепроверить число prompt tokens в реальном запросе
с конечным system prompt, шаблоном и настройками чата.
"""
import argparse
import hashlib
import json
from pathlib import Path
from urllib import request

ROOT = Path(__file__).resolve().parent
SYSTEM = "Ты участник контрольного испытания. Используй исключительно данные синтетического протокола."
PROFILES = [("dialogue-1k.txt", 1000), ("dialogue-4k.txt", 4000), ("dialogue-near-8k.txt", 7800)]


def record(n: int) -> str:
    zone = 1 + (n * 3) % 9
    rack = 1 + (n * 7) % 23
    pieces = 20 + (n * 11) % 95
    batch = (n * 13 + 17) % 997
    return (
        f"Строка {n:04d}. Условная зона З-{zone:02d}, секция С-{rack:02d}, "
        f"партия П-{batch:03d}; учтено {pieces} условных единиц. "
        "Отметка проверки: сверка выполнена, контрольный флаг нулевой. "
        "Описание целиком синтетическое; это не реальный журнал, не сведения о людях и не производственные данные.\n"
    )


def long_text(n: int) -> str:
    header = (
        "Синтетический протокол для нагрузочной проверки обработки длинного входа. "
        "Все строки — вымышленные данные для теста. Прочитай их, затем кратко "
        "опиши принцип нумерации и приведи идентификаторы строк 0001 и 0002. "
        "Не восстанавливай реальные сведения; ответ дай на русском языке.\n\n"
    )
    return header + "".join(record(i) for i in range(1, n + 1)) + "\nКонтрольный вопрос: перечисли два идентификатора первых строк и объясни, какие поля в них присутствуют."


def post_json(url: str, body: dict) -> dict:
    payload = json.dumps(body, ensure_ascii=False).encode("utf-8")
    req = request.Request(url, data=payload, headers={"Content-Type": "application/json"}, method="POST")
    with request.urlopen(req, timeout=120) as response:
        return json.load(response)


def token_count(server_url: str, message: str) -> int:
    base = server_url.rstrip("/")
    messages = [{"role": "system", "content": SYSTEM}, {"role": "user", "content": message}]
    applied = post_json(base + "/apply-template", {"messages": messages, "add_generation_prompt": True})
    prompt = applied.get("prompt")
    if not isinstance(prompt, str):
        raise ValueError("/apply-template не вернул строковое поле 'prompt'; проверьте API версии")
    toks = post_json(base + "/tokenize", {"content": prompt, "add_special": False})
    data = toks.get("tokens")
    if not isinstance(data, list):
        raise ValueError("/tokenize не вернул массив 'tokens'; проверьте API версии")
    return len(data)


def choose_n(target: int, server_url: str | None) -> tuple[int, int | None]:
    # Примерная стартовая плотность; не считать это токенизацией!
    if not server_url:
        return max(1, round(target / 55)), None
    low, high = 1, max(4, target // 12)
    while token_count(server_url, long_text(high)) < target:
        high *= 2
    best = (1, token_count(server_url, long_text(1)))
    while low <= high:
        mid = (low + high) // 2
        count = token_count(server_url, long_text(mid))
        if abs(count - target) < abs(best[1] - target):
            best = (mid, count)
        if count < target:
            low = mid + 1
        else:
            high = mid - 1
    # С запасом для near-8k, превышения допустимого целевого значения не допускаем.
    if target == 7800 and best[1] > target:
        n = best[0] - 1
        return n, token_count(server_url, long_text(n))
    return best


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server-url", help="Например http://127.0.0.1:8080, используется фактический сервер/токенизатор")
    args = parser.parse_args()
    write(ROOT / "control-short.txt", "Контрольный запрос QA. Объясни в трёх предложениях, что такое вычислительный узел локального роя, и назови два технических параметра, влияющих на скорость.\n")
    result = []
    for filename, target in PROFILES:
        n, tokens = choose_n(target, args.server_url)
        write(ROOT / filename, long_text(n) + "\n")
        result.append({
            "file": filename,
            "profile_target_prompt_tokens": target,
            "generated_records": n,
            "actual_prompt_tokens_in_test_template": tokens,
            "tokenization_status": "API_ESTIMATE_REQUIRES_FINAL_REQUEST_CHECK" if args.server_url else "UNCALIBRATED",
            "sha256": sha256(ROOT / filename)
        })
    multiturn = {
        "description": "Синтетическая фиксированная история для сравнимого измерения многоходового диалога; не переписывать ответы на реальные ответы модели внутри контрольной серии.",
        "system": SYSTEM,
        "turns": [
            {"role": "user", "content": "В условном журнале есть зоны А, Б и В. Опиши коротко структуру записи из зоны А."},
            {"role": "assistant", "content": "Запись содержит идентификатор, условную зону, номер секции, количество единиц и отметку проверки."},
            {"role": "user", "content": "Для зоны Б добавь правило: проверка выполняется после подсчёта."},
            {"role": "assistant", "content": "Для зоны Б последовательность такова: сначала подсчёт, затем проверка и фиксация результата."},
            {"role": "user", "content": "Для зоны В принято 42 единицы. Какие поля потребуются для протокола?"},
            {"role": "assistant", "content": "Идентификатор записи, зона В, секция, значение 42 и отметка о выполненной проверке."},
            {"role": "user", "content": "Подведи итог по трём зонам, перечисли принятые правила и подробно поясни, как проверить их согласованность."}
        ],
        "cache_profile": "MULTITURN_CACHE",
        "last_turn_is_target": True
    }
    (ROOT / "multiturn.json").write_text(json.dumps(multiturn, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    result.extend([
        {"file": "control-short.txt", "profile_target_prompt_tokens": None, "actual_prompt_tokens_in_test_template": None, "tokenization_status": "UNCALIBRATED", "sha256": sha256(ROOT / "control-short.txt")},
        {"file": "multiturn.json", "profile_target_prompt_tokens": None, "actual_prompt_tokens_in_test_template": None, "tokenization_status": "UNCALIBRATED", "sha256": sha256(ROOT / "multiturn.json")}
    ])
    manifest = {
        "version": "0.1",
        "intent": "Synthetic test corpus, not data from actual users",
        "system_for_api_count": SYSTEM,
        "critical_note": "Prompt counts are not valid for acceptance until rechecked with exactly the deployed chat template, thinking switch and real assembled request; prompt tokens + 256 <= 8192",
        "profile_files": result
    }
    (ROOT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for d in result:
        print(f"{d['file']}: {d.get('actual_prompt_tokens_in_test_template')} tokens; {d['tokenization_status']}")


if __name__ == "__main__":
    main()
