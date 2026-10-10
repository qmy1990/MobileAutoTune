#!/usr/bin/env python3
"""Check authored C++, Java and Swift sources with the configured formatters."""
import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--clang-format', default='clang-format')
parser.add_argument('--java-format', type=Path)
parser.add_argument('--swift-format')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
folders = ['src', 'include', 'tests', 'tools', 'android/sdk/src/main/cpp', 'ios/SDK', 'ios/Demo']
cpp = sorted(p for folder in folders for p in (root / folder).rglob('*')
             if p.suffix in {'.h', '.hpp', '.cpp', '.mm'})
subprocess.run([args.clang_format, '--dry-run', '--Werror', *map(str, cpp)], check=True)
if args.java_format:
    java = sorted(p for p in (root / 'android').rglob('*.java') if 'build' not in p.parts)
    subprocess.run(['java', '-jar', str(args.java_format), '--dry-run',
                    '--set-exit-if-changed', *map(str, java)], check=True)
if args.swift_format:
    subprocess.run([args.swift_format, '--lint', str(root / 'ios/Demo')], check=True)
print('Source formatting verified')
