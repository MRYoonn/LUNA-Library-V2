#!/usr/bin/env python3
"""Summarize strictly public, checksum-pinned native PDF4QT author extraction audits.

The full PDF text and user documents are never read here. The only accepted
inputs are this repository's manifest and the already completed public goldset
native predictions with LUNA_PUBLIC_GOLDSET_AUDIT=1.
"""
import argparse
import json
from pathlib import Path


def summarize(manifest, score, predictions):
    expected = {item["id"]: item for item in manifest}
    if len(manifest) != 31 or score.get("documents") != 31:
        raise ValueError("Only the 31 pinned public fixture corpus is authorized")
    if set(expected) != {r["id"] for r in score["rows"]}:
        raise ValueError("Wrong or incomplete public fixture IDs")
    if not all(item.get("sha256") and item.get("pdf_url") for item in manifest):
        raise ValueError("Missing public fixture verification data")
    if any(field["wrong"] for field in score["fields"].values()):
        raise ValueError("Incorrect automatic metadata: investigate before reporting")
    misses = [r for r in score["rows"] if r["fields"]["authors"] == "missing"]
    details = []
    for item in misses:
        paper = expected[item["id"]]
        actual = predictions.get(item["id"], {})
        audit = actual.get("_titleAudit", {})
        if not isinstance(audit, dict) or not isinstance(audit.get("authorRows"), list):
            raise ValueError(f"Native public-only glyph audit not enabled for {item['id']}")
        if actual.get("authors") or actual.get("authorEvidence"):
            raise ValueError(f"Unexpected previously known author evidence: {item['id']}")
        expected_last = paper["expected"]["authors"]
        if not expected_last or paper["expected"]["documentKind"] != "article":
            raise ValueError(f"Non-article author audit refused: {item['id']}")
        def bounded(rows, max_rows, max_text):
            result = []
            for row in rows[:max_rows]:
                snippet = str(row.get("text", ""))[:max_text]
                if "@" in snippet:
                    continue
                entry = {"text": snippet}
                for key in ("index", "x", "y", "fontSize"):
                    if key in row:
                        entry[key] = row[key]
                unusual = sorted({f"U+{ord(ch):04X}" for ch in snippet if ord(ch) > 127})
                if unusual:
                    entry["nonAscii"] = unusual[:25]
                result.append(entry)
            return result
        # Public-only bounded first-page author and geometric headline evidence.
        details.append({
            "id": item["id"], "publisher": paper["publisher"],
            "expectedLastAuthor": expected_last,
            "authorRows": bounded(audit["authorRows"], 75, 200),
            "headlineLines": bounded(audit.get("lines", []), 80, 180),
            "glyphRuns": bounded(audit.get("runs", []), 80, 120),
        })
    return {
        "purpose": "Public fixture native author-layout audit; not a user-library scan",
        "documents": score["documents"],
        "wholeRecordExact": score["exact"],
        "correctLastAuthors": score["fields"]["authors"]["correct"],
        "missingLastAuthors": len(misses),
        "wrongAutomaticFields": 0,
        "misses": details,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--score", type=Path, required=True)
    parser.add_argument("--predictions", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    values = [
        json.loads(path.read_text(encoding="utf-8-sig"))
        for path in (args.manifest, args.score, args.predictions)
    ]
    output = summarize(*values)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, ensure_ascii=False), encoding="utf-8")
    print("Public native author audit:", output["correctLastAuthors"], "correct,",
          output["missingLastAuthors"], "missing, zero wrong; no user files read")
    for item in output["misses"]:
        print(item["id"], item["publisher"], "authorRows=", len(item["authorRows"]),
              "headlineLines=", len(item["headlineLines"]))


if __name__ == "__main__":
    main()
