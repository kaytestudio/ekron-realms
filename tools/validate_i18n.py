#!/usr/bin/env python3
"""Validate the Android and iOS translations (English, Russian, Portuguese (Portugal),
Spanish (Spain)).

Checks:
  * Android: values/, values-ru/, values-pt-rPT/ and values-es-rES/ strings.xml define the same keys,
    no value is empty, and format placeholders (%1$s, %d, ...) match between languages.
  * Android: every R.string.<key> used in Kotlin exists in values/strings.xml.
  * iOS: every loc("key")/locF("key", ...) used in Swift exists in Localizable.xcstrings
    with en, ru, pt-PT and es-ES values, and placeholders match between languages.
  * Kotlin and Swift UI sources contain no hard-coded Cyrillic text (it belongs in the
    resource files so every language gets a translation).

Run from the repository root:  python3 tools/validate_i18n.py
The desktop build has its own check: tests/native/localization_test.cpp (ctest).
"""
from __future__ import annotations

import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ANDROID_MAIN = ROOT / "sys/android/app/src/main"
ANDROID_RES = ANDROID_MAIN / "res"
ANDROID_LOCALES = {"en": "values", "ru": "values-ru", "pt-PT": "values-pt-rPT", "es-ES": "values-es-rES"}
IOS_DIR = ROOT / "sys/ios/realms/realms iOS"
IOS_LOCALES = ("en", "ru", "pt-PT", "es-ES")

PLACEHOLDER = re.compile(r"%(?:\d+\$)?[-#+ 0,(]*\d*(?:\.\d+)?[@a-zA-Z]")
CYRILLIC = re.compile(r"[\u0400-\u04FF]")

errors: list[str] = []


def placeholders(text: str) -> list[str]:
    return sorted(p for p in PLACEHOLDER.findall(text.replace("%%", "")))


def load_android(folder: str) -> dict[str, str]:
    path = ANDROID_RES / folder / "strings.xml"
    if not path.is_file():
        errors.append(f"missing {path.relative_to(ROOT)}")
        return {}
    result: dict[str, str] = {}
    for node in ET.parse(path).getroot().iter("string"):
        name = node.get("name", "")
        if name in result:
            errors.append(f"{folder}/strings.xml: duplicate key '{name}'")
        result[name] = "".join(node.itertext())
    return result


def check_android() -> int:
    tables = {code: load_android(folder) for code, folder in ANDROID_LOCALES.items()}
    base = tables["en"]
    for code, table in tables.items():
        folder = ANDROID_LOCALES[code]
        for key in sorted(set(base) - set(table)):
            errors.append(f"Android {folder}: missing translation for '{key}'")
        for key in sorted(set(table) - set(base)):
            errors.append(f"Android {folder}: key '{key}' is not in values/strings.xml")
        for key, value in table.items():
            if not value.strip():
                errors.append(f"Android {folder}: '{key}' is empty")
            if key in base and placeholders(value) != placeholders(base[key]):
                errors.append(f"Android {folder}: placeholders of '{key}' differ from English")
    used: set[str] = set()
    for source in sorted((ANDROID_MAIN / "java").rglob("*.kt")) + sorted((ANDROID_MAIN.parent / "test").rglob("*.kt")):
        text = source.read_text(encoding="utf-8")
        used.update(re.findall(r"\bR\.string\.([A-Za-z0-9_]+)", text))
        check_no_cyrillic(source, text)
    for key in sorted(used - set(base)):
        errors.append(f"Android: R.string.{key} is used in Kotlin but not defined")
    manifest = (ANDROID_MAIN / "AndroidManifest.xml").read_text(encoding="utf-8")
    for key in re.findall(r"@string/([A-Za-z0-9_]+)", manifest):
        if key not in base:
            errors.append(f"Android manifest: @string/{key} is not defined")
    return len(base)


def check_ios() -> int:
    catalog_path = IOS_DIR / "Localizable.xcstrings"
    if not catalog_path.is_file():
        errors.append(f"missing {catalog_path.relative_to(ROOT)}")
        return 0
    strings = json.loads(catalog_path.read_text(encoding="utf-8")).get("strings", {})
    used: set[str] = set()
    for source in sorted(IOS_DIR.glob("*.swift")):
        text = source.read_text(encoding="utf-8")
        used.update(re.findall(r"\blocF?\(\s*\"([^\"]+)\"", text))
        check_no_cyrillic(source, text)
    for key in sorted(used):
        entry = strings.get(key)
        if entry is None:
            errors.append(f"iOS: key '{key}' is used in Swift but missing from Localizable.xcstrings")
            continue
        values = {}
        for code in IOS_LOCALES:
            unit = entry.get("localizations", {}).get(code, {}).get("stringUnit", {})
            value = unit.get("value", "")
            if not value.strip() or unit.get("state") != "translated":
                errors.append(f"iOS: '{key}' has no translated {code} value")
            values[code] = value
        for code in IOS_LOCALES[1:]:
            if placeholders(values[code]) != placeholders(values["en"]):
                errors.append(f"iOS: placeholders of '{key}' ({code}) differ from English")
    for key in sorted(set(strings) - used):
        errors.append(f"iOS: Localizable.xcstrings key '{key}' is not used by any Swift file")
    return len(used)


def check_no_cyrillic(source: Path, text: str) -> None:
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("*") or stripped.startswith("/*"):
            continue
        if CYRILLIC.search(line):
            errors.append(f"{source.relative_to(ROOT)}:{number}: hard-coded Cyrillic text; move it to the string resources")


def main() -> int:
    android = check_android()
    ios = check_ios()
    if errors:
        print("i18n validation failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"i18n OK: Android {android} strings and iOS {ios} strings in English, Russian, Portuguese (Portugal) and Spanish (Spain)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
