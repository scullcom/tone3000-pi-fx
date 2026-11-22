#!/usr/bin/env python3
"""Download the pinned upstream pedal images into one local TONE3000 profile."""

import argparse
import concurrent.futures
import csv
import hashlib
from pathlib import Path
import tempfile
from urllib.request import urlopen

PNG_SIGNATURE = bytes.fromhex("89504e470d0a1a0a")
MAX_BYTES = 4 * 1024 * 1024


def install(row, target):
    uri = row["LV2 plug-in URI"]
    output = target / (hashlib.sha256(uri.encode("utf-8")).hexdigest() + ".png")
    if output.is_file():
        return
    url = row["Screenshot URL"] or row["Thumbnail URL"]
    with urlopen(url, timeout=30) as response:
        data = response.read(MAX_BYTES + 1)
    if not data.startswith(PNG_SIGNATURE) or len(data) > MAX_BYTES:
        raise ValueError("Upstream artwork is not a supported PNG")
    with tempfile.NamedTemporaryFile(dir=target, delete=False) as temporary:
        staging = Path(temporary.name)
        try:
            temporary.write(data)
            temporary.close()
            staging.replace(output)
        finally:
            staging.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path, help="TONE3000 profile directory")
    args = parser.parse_args()
    target = args.profile / "lv2-artwork" / "guitarix"
    target.mkdir(parents=True, exist_ok=True)
    with Path(__file__).with_name("guitarix-artwork-map.csv").open(encoding="utf-8-sig", newline="") as source:
        rows = list(csv.DictReader(source))

    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        tasks = {pool.submit(install, row, target): row for row in rows}
        for task in concurrent.futures.as_completed(tasks):
            try:
                task.result()
            except (OSError, ValueError) as error:
                failed += 1
                print(f'{tasks[task]["LV2 bundle"]}: {error}')
    print(f"{len(rows) - failed} of {len(rows)} pedal images available locally")
    return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
