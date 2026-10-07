"""Puts Tessera's pixel-art library (tspixel, in the Tessera plugin's Tools) on the import path."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "unreal", "Plugins", "Tessera", "Tools", "pixelart"))
