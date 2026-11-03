#!/usr/bin/env python3
"""Run from a checkout with Python, or DragonRadio's embedded interpreter."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "dragonradio"))

from dragonradio.simulation.__main__ import main

if __name__ == "__main__":
    raise SystemExit(main())
