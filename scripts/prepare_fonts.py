"""Generate bundled static faces (build-time only; requires fonttools 4.60.1).

Sources: Google Fonts ofl/rubik and ofl/onest, retrieved 2026-10-18.
Original variable files and SIL OFL notices are retained in the repository.
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "third_party/font-tools"))
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

report = []
for family, weights in (("Rubik", (300, 400, 500, 600)), ("Onest", (400,))):
    source = ROOT / f"third_party/fonts/{family}-Variable.ttf"
    for weight in weights:
        font = TTFont(source, recalcTimestamp=False)
        static = instantiateVariableFont(font, {"wght": weight}, inplace=False, updateFontNames=True)
        output = ROOT / f"resources/fonts/{family}-{weight}.ttf"
        static.save(output)
        names = {str(i): static["name"].getDebugName(i) for i in (1, 2, 6, 16, 17)}
        sample = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzЁёАБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмнопрстуфхцчшщъыьэюя0123456789…—·%"
        assert all(ord(c) in static.getBestCmap() for c in sample), family
        assert static["OS/2"].usWeightClass == weight
        report.append(dict(file=str(output.relative_to(ROOT)), bytes=output.stat().st_size,
                           sha256=hashlib.sha256(output.read_bytes()).hexdigest(), names=names,
                           sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest()))
(ROOT / "resources/fonts/manifest.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
print(json.dumps(report, ensure_ascii=False, indent=2))
