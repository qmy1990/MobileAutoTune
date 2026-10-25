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
    relative = os.path.relpath(os.path.realpath(str(path)), os.path.realpath(str(directory)))
    return relative != os.pardir and not relative.startswith(os.pardir + os.sep)


def fetch(target, urls, checksum):
    if target.is_file() and digest(target) == checksum:
        return
    os.makedirs(str(target.parent), exist_ok=True)
    with tempfile.TemporaryDirectory(dir=str(target.parent), prefix='.download-') as work:
        temporary = Path(work) / target.name
        for url in urls:
            return_code = subprocess.call([
                'curl', '-fL', '--silent', '--show-error', '--connect-timeout', '12',
                '--max-time', '900', '--speed-time', '30', '--speed-limit', '1024',
                url, '-o', str(temporary),
            ])
            if return_code == 0 and digest(temporary) == checksum:
                os.replace(str(temporary), str(target))
                return
        raise SystemExit('Cannot fetch verified dependency: {}'.format(target.name))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['android', 'ios', 'desktop', 'models', 'all'], default='all')
    parser.add_argument('--runtime-only', action='store_true', help='Prepare runtime libraries without model downloads')
    args = parser.parse_args()
    if args.runtime_only and args.platform == 'models':
        parser.error('--runtime-only requires a runtime platform')
    with (MODELS / 'manifest.json').open() as source:
        manifest = json.load(source)
    for model in ([] if args.runtime_only else manifest['models']):
        fetch(MODELS / model['file'], model['urls'], model['sha256'])
    for license_file in (ROOT / 'third_party/key_models').glob('*LICENSE'):
        shutil.copy2(str(license_file), str(MODELS / license_file.name))
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
        if stamp.is_file() and (directory / spec['marker']).is_file():
            with stamp.open() as source:
                cached = json.load(source)
            if cached.get('archive_sha256') == spec['sha256'] and cached.get('symlinks') is True:
                continue
        archive = CACHE / spec['file']
        fetch(archive, [spec['url']], spec['sha256'])
        os.makedirs(str(directory), exist_ok=True)
        with zipfile.ZipFile(str(archive)) as package:
            for name in package.namelist():
                target = directory / name
                if not within(target, directory):
                    raise SystemExit('Invalid runtime archive path')
            package.extractall(str(directory))
            for item in package.infolist():
                if stat.S_ISLNK(item.external_attr >> 16):
                    target = directory / item.filename
                    link = package.read(item).decode('utf-8')
                    if not within(target.parent / link, directory):
                        raise SystemExit('Invalid runtime symlink')
                    if target.exists() or target.is_symlink():
                        target.unlink()
                    target.symlink_to(link)
        with stamp.open('w') as output:
            output.write(json.dumps({'archive_sha256': spec['sha256'], 'symlinks': True}) + '\n')
    print('Verified key models and inference runtimes are ready')


if __name__ == '__main__':
    main()
