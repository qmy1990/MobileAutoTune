#!/usr/bin/env python3
"""Package freshly built SDKs and unsigned iOS demos; never bundle user audio."""
import argparse, hashlib, json, pathlib, re, shutil, struct, tempfile, zipfile
root = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--platform', choices=['android', 'ios', 'all'], default='all')
args = parser.parse_args()
version = re.search(r'project\(MobileAutoTune VERSION ([0-9.]+)', (root / 'CMakeLists.txt').read_text()).group(1)
out = root / 'dist/release'
out.mkdir(parents=True, exist_ok=True)
abis = ['armeabi-v7a', 'arm64-v8a', 'x86', 'x86_64']

def zip_tree(source, target):
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(source.rglob('*')):
            if p.is_file() and p.name != '.DS_Store':
                z.write(p, p.relative_to(source))

def notices(target):
    for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md']:
        shutil.copy2(root / name, target / name)

if args.platform in ['android', 'all']:
    aar = root / 'dist/android/mobileaudio-sdk.aar'
    with zipfile.ZipFile(aar) as z:
        actual = {n.split('/')[1] for n in z.namelist() if n.startswith('jni/') and n.endswith('.so')}
        assert actual == set(abis), actual
        for name in z.namelist():
            if name.endswith('.so') and name.split('/')[1] in ['arm64-v8a', 'x86_64']:
                data = z.read(name)
                assert data[:5] == b'\x7fELF\x02', name
                offset = struct.unpack_from('<Q', data, 32)[0]
                size, count = struct.unpack_from('<HH', data, 54)
                writable, relro = [], []
                for i in range(count):
                    header = offset + i * size
                    kind = struct.unpack_from('<I', data, header)[0]
                    if kind == 1:
                        assert struct.unpack_from('<Q', data, header + 48)[0] >= 16384, name
                        if struct.unpack_from('<I', data, header + 4)[0] & 2:
                            address = struct.unpack_from('<Q', data, header + 16)[0]
                            memory = struct.unpack_from('<Q', data, header + 40)[0]
                            writable.append((address, address + memory))
                    if kind == 0x6474e552:  # PT_GNU_RELRO
                        address = struct.unpack_from('<Q', data, header + 16)[0]
                        memory = struct.unpack_from('<Q', data, header + 40)[0]
                        relro.append((address, address + memory))
                # lld may put subsequent writable data in a separate LOAD with a
                # full-page gap. Rounded RELRO protection must not cover it.
                for begin, end in relro:
                    rounded_begin, rounded_end = begin // 16384 * 16384, (end + 16383) // 16384 * 16384
                    for low, high in writable:
                        assert not (max(low, end) < min(high, rounded_end)), name
                        assert not (max(low, rounded_begin) < min(high, begin)), name
        assert 'assets/LICENSE' in z.namelist()
        shutil.copy2(aar, out / f'mobileautotune-v{version}.aar')
        for abi in abis:
            with zipfile.ZipFile(out / f'mobileautotune-v{version}-{abi}.aar', 'w', zipfile.ZIP_DEFLATED) as dest:
                for item in z.infolist():
                    if item.filename.startswith('jni/') and not item.filename.startswith(f'jni/{abi}/'):
                        continue
                    dest.writestr(item, z.read(item.filename))
    apk = root / 'dist/android/mobileaudio-demo.apk'
    with zipfile.ZipFile(apk) as z, apk.open('rb') as raw:
        for item in z.infolist():
            if item.filename.startswith(('lib/arm64-v8a/', 'lib/x86_64/')) and item.filename.endswith('.so') and item.compress_type == zipfile.ZIP_STORED:
                raw.seek(item.header_offset + 26)
                name_size, extra_size = struct.unpack('<HH', raw.read(4))
                assert (item.header_offset + 30 + name_size + extra_size) % 16384 == 0, item.filename
    shutil.copy2(apk, out / f'MobileAutoTune-demo-v{version}.apk')
    with tempfile.TemporaryDirectory() as td:
        stage = pathlib.Path(td)
        shutil.copytree(root / 'include', stage / 'include')
        notices(stage)
        for abi in abis:
            candidates = list((root / 'android/sdk/.cxx/RelWithDebInfo').glob(f'*/{abi}/core/libmobileautotune.a'))
            if not candidates:
                raise RuntimeError(f'No release native build for {abi}')
            core = max(candidates, key=lambda p: p.stat().st_mtime).parent
            libdir = stage / 'lib' / abi
            libdir.mkdir(parents=True)
            for name in ['libmobileautotune.a', 'libmat_rnnoise.a', 'libmat_rnnoise_legacy.a']:
                shutil.copy2(core / name, libdir / name)
        (stage / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.22)
project(MobileAutoTunePrebuilt LANGUAGES C CXX)
if(NOT ANDROID)
  message(FATAL_ERROR "Use an Android NDK toolchain")
endif()
foreach(name mobileautotune mat_rnnoise mat_rnnoise_legacy)
  add_library(${name} STATIC IMPORTED GLOBAL)
  set_target_properties(${name} PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib/${ANDROID_ABI}/lib${name}.a")
endforeach()
set_target_properties(mobileautotune PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/include"
  INTERFACE_LINK_LIBRARIES "mat_rnnoise;mat_rnnoise_legacy;m")
if(ANDROID_ABI STREQUAL "arm64-v8a" OR ANDROID_ABI STREQUAL "x86_64")
  set_property(TARGET mobileautotune APPEND PROPERTY INTERFACE_LINK_OPTIONS "-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")
endif()
''')
        (stage / 'README.md').write_text('''# Android native C/C++ SDK
Android API26+, armeabi-v7a/arm64-v8a/x86/x86_64. Includes the DSP core and both RNNoise implementations.
Use the Android NDK CMake toolchain and add_subdirectory(this package), then target_link_libraries(your_target PRIVATE mobileautotune).
Link C++ runtime consistently with your host. The core uses C++17 and exposes the C API in include/mat/.
The native core does not capture audio: supply your own 48kHz mono float32 I/O, or use the Java AAR for Oboe live recording.
Redistribute LICENSE and THIRD_PARTY_NOTICES.md with your application.
''')
        zip_tree(stage, out / f'MobileAutoTune-android-native-v{version}.zip')

if args.platform in ['ios', 'all']:
    with tempfile.TemporaryDirectory() as td:
        stage = pathlib.Path(td)
        shutil.copytree(root / 'dist/ios/MobileAudioSDK.xcframework', stage / 'MobileAudioSDK.xcframework')
        notices(stage)
        zip_tree(stage, out / f'MobileAudioSDK-v{version}.xcframework.zip')
    for platform, sdk in [('device', 'iphoneos'), ('simulator', 'iphonesimulator')]:
        app = root / f'build-ios-{platform}/Release-{sdk}/MobileAudioDemo.app'
        with tempfile.TemporaryDirectory() as td:
            stage = pathlib.Path(td)
            parent = stage / 'Payload' if platform == 'device' else stage
            parent.mkdir(exist_ok=True)
            target = parent / app.name
            shutil.copytree(app, target)
            # Public demos contain no developer profiles, device IDs or signatures.
            for p in list(target.rglob('embedded.mobileprovision')):
                p.unlink()
            for p in list(target.rglob('_CodeSignature')):
                shutil.rmtree(p)
            if platform == 'simulator':
                notices(stage)
            name = f'MobileAutoTune-demo-v{version}-unsigned.ipa' if platform == 'device' else f'MobileAutoTune-demo-v{version}-simulator.zip'
            zip_tree(stage, out / name)

notices(out)
files = [p for p in out.iterdir() if p.is_file() and p.name not in ['SHA256SUMS', 'manifest.json']]
manifest = {'version': version, 'android_min_api': 26, 'ios_min_version': '14.0', 'files': {}}
for p in sorted(files):
    manifest['files'][p.name] = {'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(out / 'SHA256SUMS').write_text(''.join(f"{v['sha256']}  {n}\n" for n, v in manifest['files'].items()))
print(f'Packaged {len(files)} files in {out}')
