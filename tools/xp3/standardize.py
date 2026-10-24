#!/usr/bin/env python3
"""Package a game using the same host normalize-content implementation as online import.

Python owns only ZIP reception, file facts, verification and STORE publication.
Format probes, name mapping and resource decoding run in the explicit runtime tool.
No game executable, DLL, TPM or script is executed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

BUFFER = 1024 * 1024
MAX_FILE = 8 * 1024 ** 3
MAX_TREE = 64 * 1024 ** 3
MAX_MESSAGE = 64 * 1024 ** 2


class FormatError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise FormatError(message)


def safe_path(name):
    require(isinstance(name, str) and 0 < len(name) <= 4096
            and not name.startswith('/') and ':' not in name and "\\" not in name
            and all(ord(c) >= 32 and ord(c) != 127 for c in name)
            and all(part not in ('', '.', '..') for part in name.split('/')),
            'unsafe logical path')


def sha256_file(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def receive(source_path, root):
    files, locators, seen, total = [], {}, set(), 0
    with zipfile.ZipFile(source_path) as archive:
        for info in archive.infolist():
            name = info.filename.rstrip('/') if info.is_dir() else info.filename
            safe_path(name)
            require(info.filename not in seen, 'duplicate ZIP member')
            seen.add(info.filename)
            mode = (info.external_attr >> 16) & 0xffff
            require(not stat.S_ISLNK(mode) and not info.flag_bits & 1,
                    'ZIP symlinks and encrypted members unsupported')
            if info.is_dir():
                continue
            require(info.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)
                    and 0 <= info.file_size <= MAX_FILE, 'unsupported ZIP method or size')
            total += info.file_size
            require(total <= MAX_TREE and len(files) < 100000, 'game reception limit')
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            digest, count = hashlib.sha256(), 0
            # ZipFile also verifies the original member CRC at EOF.
            with archive.open(info) as src, target.open('xb') as dst:
                while data := src.read(BUFFER):
                    count += len(data)
                    require(count <= info.file_size, 'ZIP expanded size mismatch')
                    dst.write(data)
                    digest.update(data)
            require(count == info.file_size, 'ZIP truncated member')
            files.append(dict(logicalKey=name, name=name, sizeBytes=count, sha256=digest.hexdigest()))
            locators[name] = dict(path=str(target), offset=0, sizeBytes=count)
    require(files, 'empty game')
    files.sort(key=lambda file: file['logicalKey'].encode('utf-8'))
    return files, locators


def normalize(args, files, locators, output_root):
    request = json.dumps(dict(platformId=args.platform, files=files, locators=locators,
                              outputRoot=str(output_root)), ensure_ascii=False).encode('utf-8')
    require(len(request) <= MAX_MESSAGE, 'runtime request limit')
    process = subprocess.Popen([args.node, str(args.runtime_tool), 'normalize-content'],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        output, _ = process.communicate(request, timeout=60 * 60)
    except BaseException:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        raise
    require(len(output) <= MAX_MESSAGE, 'runtime response limit')
    response = json.loads(output)
    require(process.returncode == 0 and 'error' not in response,
            response.get('error', 'runtime normalization failed'))
    require(isinstance(response.get('files'), list) and response['files'], 'invalid normalized manifest')
    return response


def write_package(target, response, originals, converted_root):
    seen, total = set(), 0
    with zipfile.ZipFile(target, 'x', compression=zipfile.ZIP_STORED, allowZip64=True) as package:
        for file in sorted(response['files'], key=lambda item: item['logicalKey'].encode('utf-8')):
            name = file['logicalKey']
            safe_path(name)
            require(name not in seen and len(seen) < 100000, 'duplicate output file')
            seen.add(name)
            require(type(file['sizeBytes']) is int and 0 <= file['sizeBytes'] <= MAX_FILE,
                    'invalid output size')
            total += file['sizeBytes']
            require(total <= MAX_TREE, 'output reception limit')
            if file.get('path'):
                path = Path(file['path'])
                require(path == converted_root / name and path.resolve() == path
                        and path.is_file() and not path.is_symlink(), 'output outside owned staging')
            else:
                require(name in originals, 'unknown unchanged output')
                original, path = originals[name]
                require(all(file[key] == original[key] for key in ('sizeBytes', 'sha256')),
                        'unchanged file facts differ')
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_STORED
            entry.external_attr = (stat.S_IFREG | 0o600) << 16
            digest, count = hashlib.sha256(), 0
            with path.open('rb') as src, package.open(entry, 'w', force_zip64=True) as dst:
                while data := src.read(BUFFER):
                    count += len(data)
                    require(count <= file['sizeBytes'], 'normalized output grew')
                    digest.update(data)
                    dst.write(data)
            require(count == file['sizeBytes'] and digest.hexdigest() == file['sha256'],
                    'normalized output content changed')


def fsync_directory(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def standardize(args):
    require(args.source != args.output and args.report not in (args.source, args.output),
            'source, output and report must differ')
    require(not args.output.exists() and not args.report.exists(), 'output or report already exists')
    require(args.output.parent.is_dir() and args.report.parent.is_dir(), 'output parents must exist')
    require(args.report.parent.stat().st_dev == args.output.parent.stat().st_dev,
            'report and output require the same filesystem')
    source_sha = sha256_file(args.source)
    with tempfile.TemporaryDirectory(prefix='.normalize-content-', dir=args.output.parent) as temporary:
        root = Path(temporary)
        received, converted = root / 'received', root / 'converted'
        received.mkdir(mode=0o700)
        files, locators = receive(args.source, received)
        response = normalize(args, files, locators, converted)
        candidate, staged_report = root / 'candidate.zip', root / 'conversion.json'
        originals = {file['logicalKey']: (file, Path(locators[file['logicalKey']]['path'])) for file in files}
        write_package(candidate, response, originals, converted)
        require(sha256_file(args.source) == source_sha, 'source changed during conversion')
        report = dict(status='normalized' if response.get('normalization') else 'unchanged',
                      sourceSha256=source_sha, sourceUnchanged=True,
                      outputSha256=sha256_file(candidate), outputSize=candidate.stat().st_size,
                      normalization=response.get('normalization'),
                      files=[{key: file[key] for key in ('logicalKey', 'sizeBytes', 'sha256')}
                             for file in response['files']])
        with staged_report.open('x', encoding='utf-8') as destination:
            json.dump(report, destination, ensure_ascii=False, indent=2)
            destination.write('\n')
            destination.flush()
            os.fsync(destination.fileno())
        with candidate.open('rb') as source:
            os.fsync(source.fileno())
        # Publish the report first and never overwrite concurrent files.
        os.link(staged_report, args.report)
        try:
            fsync_directory(args.report.parent)
            os.link(candidate, args.output)
        except OSError:
            try:
                published, staged = args.report.lstat(), staged_report.stat()
                if (stat.S_ISREG(published.st_mode) and
                        (published.st_dev, published.st_ino) == (staged.st_dev, staged.st_ino)):
                    args.report.unlink()
            except OSError:
                pass
            raise
        fsync_directory(args.output.parent)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--runtime-tool', type=Path, required=True,
                        help='explicit packaged retrom-runtime scripts/runtime-cli.mjs')
    parser.add_argument('--node', default=shutil.which('node') or 'node')
    parser.add_argument('--platform', default='kirikiri')
    args = parser.parse_args()
    args.source = args.source.resolve(strict=True)
    args.runtime_tool = args.runtime_tool.resolve(strict=True)
    args.output, args.report = args.output.resolve(), args.report.resolve()
    try:
        report = standardize(args)
    except (FormatError, OSError, ValueError, zipfile.BadZipFile) as error:
        print(f'normalization failed: {error}', file=sys.stderr)
        return 1
    print(json.dumps({key: value for key, value in report.items() if key not in ('files', 'normalization')},
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
