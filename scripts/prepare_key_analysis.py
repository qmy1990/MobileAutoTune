#!/usr/bin/env python3
"""Prepare checksum-pinned FP32 key models and platform inference runtimes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.tools/key-downloads'
ORT = ROOT / '.tools/ort'
MODELS = ROOT / 'models/key'
RUNTIMES = {
    'android': {
        'file': 'onnxruntime-android-1.22.0.aar',
        'url': 'https://repo.maven.apache.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.22.0/onnxruntime-android-1.22.0.aar',
        'sha256': '04a4617a9c797cf49225595e45b5546081cb34c86ac817581141577d3b7dbfe2',
        'marker': 'jni/arm64-v8a/libonnxruntime.so',
    },
    'ios': {
        'file': 'onnxruntime-c-1.20.0.zip',
        'url': 'https://download.onnxruntime.ai/pod-archive-onnxruntime-c-1.20.0.zip',
        'sha256': '50891a8aadd17d4811acb05ed151ba6c394129bb3ab14e843b0fc83a48d450ff',
        'marker': 'onnxruntime.xcframework/ios-arm64/onnxruntime.framework/onnxruntime',
    },
}


def digest(path):
    checksum = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            checksum.update(block)
    return checksum.hexdigest()


def within(path, directory):
    try:
        path.resolve().relative_to(directory.resolve())
        return True
    except ValueError:
        return False


def fetch(target, urls, checksum):
    if target.is_file() and digest(target) == checksum:
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent, prefix='.download-') as work:
        temporary = Path(work) / target.name
        for url in urls:
            process = subprocess.run([
                'curl', '-fL', '--silent', '--show-error', '--connect-timeout', '12',
                '--max-time', '900', '--speed-time', '30', '--speed-limit', '1024',
                url, '-o', str(temporary),
            ])
            if process.returncode == 0 and digest(temporary) == checksum:
                os.replace(temporary, target)
                return
        raise SystemExit(f'Cannot fetch verified dependency: {target.name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['android', 'ios', 'desktop', 'models', 'all'], default='all')
    parser.add_argument('--runtime-only', action='store_true', help='Prepare runtime libraries without model downloads')
    args = parser.parse_args()
    if args.runtime_only and args.platform == 'models':
        parser.error('--runtime-only requires a runtime platform')
    manifest = json.loads((MODELS / 'manifest.json').read_text())
    for model in ([] if args.runtime_only else manifest['models']):
        fetch(MODELS / model['file'], model['urls'], model['sha256'])
    for license_file in (ROOT / 'third_party/key_models').glob('*LICENSE'):
        shutil.copy2(license_file, MODELS / license_file.name)
    if args.platform == 'models':
        print('Verified key models are ready')
        return
    platforms = ['android', 'ios'] if args.platform == 'all' else [args.platform]
    if platforms == ['desktop']:
        if os.uname().sysname != 'Darwin':
            raise SystemExit('Set MAT_ORT_LIBRARY to a compatible local ONNX Runtime on this desktop')
        platforms = ['ios']
    for platform in platforms:
        spec = RUNTIMES[platform]
        directory = ORT / platform
        stamp = directory / '.verified-runtime.json'
        if stamp.is_file() and json.loads(stamp.read_text()).get('archive_sha256') == spec['sha256'] and json.loads(stamp.read_text()).get('symlinks') is True and (directory / spec['marker']).is_file():
            continue
        archive = CACHE / spec['file']
        fetch(archive, [spec['url']], spec['sha256'])
        directory.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive) as package:
            for name in package.namelist():
                target = directory / name
                if not within(target, directory):
                    raise SystemExit('Invalid runtime archive path')
            package.extractall(directory)
            for item in package.infolist():
                if stat.S_ISLNK(item.external_attr >> 16):
                    target = directory / item.filename
                    link = package.read(item).decode('utf-8')
                    if not within(target.parent / link, directory):
                        raise SystemExit('Invalid runtime symlink')
                    target.unlink(missing_ok=True)
                    target.symlink_to(link)
        stamp.write_text(json.dumps({'archive_sha256': spec['sha256'], 'symlinks': True}) + '\n')
    print('Verified key models and inference runtimes are ready')


if __name__ == '__main__':
    main()
