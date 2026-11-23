"""pytest setup for the Home Assistant integration tests.

Needs: pip install pytest-homeassistant-custom-component (see Makefile test-ha).
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

