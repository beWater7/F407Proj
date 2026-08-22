"""Force pyOCD to use Linux hidraw backend (works with /dev/hidraw* udev rules)."""
import hidraw as _hidraw
import sys

sys.modules[__name__] = _hidraw
