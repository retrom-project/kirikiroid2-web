#!/usr/bin/env python3
"""Offline CxFilterFS/eliF -> ordinary XP3, inside a new STORE ZIP.

This is an explicit format converter, not a runtime filter or game detector.
It never executes the input EXE, TPM, DLL, or scripts. Python standard library only.
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
import tempfile
import zipfile
import zlib

MAGIC = bytes.fromhex('5850330d0a200a1a8b6701')
BUFFER = 1024 * 1024
MAX_INDEX = 32 * BUFFER


class FormatError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise FormatError(message)


def normalize(name):
    # CxFilterFS folds ASCII only; Unicode lower()/casefold() changes other names.
    name = ''.join(chr(ord(c) + 32) if 'A' <= c <= 'Z' else c for c in name)
    return re.sub('/+', '/', name.replace('\\', '/')).lstrip('/')


def safe_path(name):
    require(name and '\x00' not in name and '\\' not in name
            and not name.startswith('/') and ':' not in name
            and all(p not in ('', '.', '..') for p in name.rstrip('/').split('/')),
            f'unsafe virtual path: {name!r}')


def filter_key(h):
    t = h ^ 0x1548E29C
    return ((t ^ (t >> 8) ^ (t >> 16) ^ (t >> 24)) & 255) or 0xD7


def sha256_file(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


class Slice:
    def __init__(self, file, start, size):
        self.file, self.start, self.size = file, start, size

    def read(self, offset, size):
        require(0 <= offset <= self.size and 0 <= size <= self.size - offset,
                'XP3 read outside ZIP member')
        self.file.seek(self.start + offset)
        data = self.file.read(size)
        require(len(data) == size, 'short source read')
        return data


def zip_slice(file, info):
    require(info.compress_type == zipfile.ZIP_STORED and not info.flag_bits & 1,
            f'input must be an unencrypted STORE ZIP: {info.filename}')
    file.seek(info.header_offset)
    header = file.read(30)
    require(len(header) == 30 and header[:4] == b'PK\x03\x04', 'invalid ZIP local header')
    flags, method = struct.unpack_from('<HH', header, 6)
    n, extra = struct.unpack_from('<HH', header, 26)
    require(method == info.compress_type and flags == info.flag_bits,
            'ZIP local/central header mismatch')
    encoding = 'utf-8' if flags & 0x800 else 'cp437'
    require(file.read(n).decode(encoding) == info.orig_filename, 'ZIP local name mismatch')
    return Slice(file, info.header_offset + 30 + n + extra, info.file_size)


def chunks(data):
    at = 0
    while at < len(data):
        require(at + 12 <= len(data), 'short XP3 chunk header')
        tag, size = struct.unpack_from('<4sQ', data, at)
        at += 12
        require(size <= len(data) - at, 'XP3 chunk out of bounds')
        yield tag, data[at:at + size]
        at += size


def index_blocks(source):
    require(source.read(0, 11) == MAGIC, 'unsupported XP3 signature')
    offset = struct.unpack('<Q', source.read(11, 8))[0]
    visited, total = set(), 0
    for _ in range(16):
        require(offset not in visited, 'XP3 index cycle')
        visited.add(offset)
        flag = source.read(offset, 1)[0]
        require(flag & ~0x81 == 0, 'unsupported XP3 index flags')
        at = offset + 1
        if flag & 1:
            packed, size = struct.unpack('<QQ', source.read(at, 16))
            at += 16
            require(packed <= MAX_INDEX and size <= MAX_INDEX - total, 'XP3 index limit')
            decoder = zlib.decompressobj()
            data = decoder.decompress(source.read(at, packed), size + 1)
            require(len(data) == size and decoder.eof and not decoder.unconsumed_tail
                    and not decoder.unused_data, 'invalid compressed XP3 index')
            at += packed
        else:
            size = struct.unpack('<Q', source.read(at, 8))[0]
            at += 8
            require(size <= MAX_INDEX - total, 'XP3 index limit')
            data = source.read(at, size)
            at += size
        total += size
        yield data
        if not flag & 0x80:
            return
        offset = struct.unpack('<Q', source.read(at, 8))[0]
    raise FormatError('XP3 index chain limit')


def utf16_field(data, offset, count):
    require(0 < count <= 32767 and offset + count * 2 <= len(data), 'invalid UTF-16 name length')
    end = offset + count * 2
    require(data[end:] in (b'', b'\0\0'), 'unknown bytes following XP3 name')
    name = data[offset:end].decode('utf-16-le', errors='strict')
    require('\0' not in name, 'embedded NUL in XP3 name')
    return name


@dataclass
class Entry:
    physical: str
    flags: int
    declared_size: int
    packed_size: int
    segments: list
    adler: int
    timestamp: bytes | None
    name: str = ''
    h: int = 0

    @property
    def readable_size(self):
        return sum(s[2] for s in self.segments)


def read_entries(source):
    files, mappings = {}, {}
    for block in index_blocks(source):
        for tag, data in chunks(block):
            if tag == b'eliF':
                require(len(data) >= 6, 'short eliF')
                h, count = struct.unpack_from('<IH', data)
                name = normalize(utf16_field(data, 6, count))
                safe_path(name)
                require(not name.endswith('/'), 'directory presented as an eliF file')
                require(name not in mappings, f'duplicate logical name: {name}')
                mappings[name] = h
            elif tag == b'File':
                fields = {}
                for key, value in chunks(data):
                    require(key in (b'info', b'segm', b'adlr', b'time') and key not in fields,
                            f'unsupported or duplicate File field: {key!r}')
                    fields[key] = value
                require({b'info', b'segm', b'adlr'} <= fields.keys(), 'incomplete File')
                info, segm = fields[b'info'], fields[b'segm']
                require(len(info) >= 22 and len(segm) > 0 and len(segm) % 28 == 0
                        and len(fields[b'adlr']) == 4, 'invalid File field size')
                flags, size, packed, count = struct.unpack_from('<IQQH', info)
                physical = utf16_field(info, 22, count)
                require(re.fullmatch('[0-9a-f]{32}', physical) is not None,
                        'CxFilterFS input requires MD5 physical names')
                require(physical not in files, 'duplicate physical name')
                require(flags in (0, 0x80000000), 'unsupported File flags')
                segments = list(struct.iter_unpack('<IQQQ', segm))
                for mode, offset, original, stored in segments:
                    require(mode in (0, 1), 'unsupported segment mode')
                    require(mode == 1 or original == stored, 'raw segment size mismatch')
                    require(offset <= source.size and stored <= source.size - offset,
                            'segment outside XP3')
                timestamp = fields.get(b'time')
                require(timestamp is None or len(timestamp) == 8, 'invalid timestamp')
                files[physical] = Entry(physical, flags, size, packed, segments,
                                        struct.unpack('<I', fields[b'adlr'])[0], timestamp)
            else:
                raise FormatError(f'unsupported index chunk {tag!r}; not this format')
    require(files and len(files) == len(mappings), 'missing or incomplete eliF mapping')
    linked = set()
    for name, h in mappings.items():
        physical = hashlib.md5(name.encode('utf-16-le')).hexdigest()
        require(physical in files and physical not in linked, 'eliF/physical mapping mismatch')
        entry = files[physical]
        entry.name, entry.h = name, h
        linked.add(physical)
    return list(files.values())


def segment_bytes(source, segment):
    mode, offset, expanded, packed = segment
    decoder = zlib.decompressobj() if mode else None
    produced = 0
    for at in range(0, packed, BUFFER):
        pending = source.read(offset + at, min(BUFFER, packed - at))
        while pending:
            if decoder:
                block = decoder.decompress(pending, BUFFER)
                pending = decoder.unconsumed_tail
            else:
                block, pending = pending, b''
            produced += len(block)
            require(produced <= expanded, 'segment expands beyond declared size')
            if block:
                yield block
    require(produced == expanded, 'segment expanded size mismatch')
    require(not decoder or (decoder.eof and not decoder.unused_data), 'incomplete zlib segment')


def chunk(tag, value):
    return struct.pack('<4sQ', tag, len(value)) + value


def convert_archive(source, entries, target, policy):
    records, audit = [], []
    target.write(MAGIC + b'\0' * 8)
    for entry in entries:
        short = entry.readable_size != entry.declared_size
        require(not short or (policy == 'native-filtered-short-stream'
                              and entry.readable_size < entry.declared_size),
                f'inconsistent entry size: {entry.name}')
        offset, count, adler = target.tell(), 0, 1
        raw_hash, decoded_hash = hashlib.sha256(), hashlib.sha256()
        key = filter_key(entry.h)
        table = bytes(b ^ key for b in range(256))
        for segment in entry.segments:
            for raw in segment_bytes(source, segment):
                # Native CxFilterFS applies this even when File.flags == 0.
                decoded = raw.translate(table)
                raw_hash.update(raw)
                decoded_hash.update(decoded)
                target.write(decoded)
                count += len(decoded)
                adler = zlib.adler32(decoded, adler)
        adler &= 0xffffffff
        require(short or (adler == entry.adler == entry.h),
                f'decoded Adler32 mismatch: {entry.name}')
        encoded_name = entry.name.encode('utf-16-le')
        fields = chunk(b'info', struct.pack('<IQQH', 0, count, count,
                                            len(encoded_name) // 2) + encoded_name)
        fields += chunk(b'segm', struct.pack('<IQQQ', 0, offset, count, count))
        fields += chunk(b'adlr', struct.pack('<I', adler))
        if entry.timestamp is not None:
            fields += chunk(b'time', entry.timestamp)
        records.append(chunk(b'File', fields))
        audit.append({'name': entry.name, 'physicalName': entry.physical,
                      'status': 'native-filtered-short-stream' if short else 'decoded-verified',
                      'sourceFlags': entry.flags, 'sourceDeclaredSize': entry.declared_size,
                      'sourcePackedSize': entry.packed_size, 'sourceSegments': entry.segments,
                      'sourceAdler32': entry.adler, 'eliFHash': entry.h, 'key': key,
                      'readableSize': count, 'outputAdler32': adler,
                      'rawStreamSha256': raw_hash.hexdigest(),
                      'decodedSha256': decoded_hash.hexdigest(),
                      'sourceIntegrityVerified': not short,
                      'metadataCorrected': short, 'transformApplied': True})
    index_offset = target.tell()
    index = b''.join(records)
    require(len(index) <= MAX_INDEX, 'output index exceeds limit')
    target.write(b'\0' + struct.pack('<Q', len(index)) + index)
    target.seek(11)
    target.write(struct.pack('<Q', index_offset))
    target.flush()
    return audit


def standardize(source_path, output_path, report_path, report, policy):
    require(source_path.resolve() != output_path.resolve(), 'input and output must differ')
    require(not output_path.exists(), 'output exists; refusing to overwrite')
    require(output_path.parent.is_dir(), 'output parent must exist')
    identity = source_path.stat()
    report.update(format='cxfilterfs-elif-tinyxor-v1', policy=policy,
                  source=str(source_path.resolve()), sourceSize=identity.st_size,
                  archives=[], copiedMembers=[])
    with source_path.open('rb') as raw, zipfile.ZipFile(source_path) as archive:
        plans, seen = [], set()
        for info in archive.infolist():
            safe_path(info.filename)
            require(info.filename not in seen, 'duplicate outer ZIP member')
            seen.add(info.filename)
            mode = (info.external_attr >> 16) & 0xffff
            require(not stat.S_ISLNK(mode), 'ZIP symlinks unsupported')
            member = zip_slice(raw, info)
            require(Path(info.filename).name.lower() != 'xp3filter.tjs',
                    'additional loose xp3filter.tjs requires separate analysis')
            entries = read_entries(member) if info.filename.lower().endswith('.xp3') else None
            if entries is not None:
                require(all(Path(e.name).name != 'xp3filter.tjs' for e in entries),
                        'additional embedded xp3filter.tjs requires separate analysis')
                for entry in entries:
                    packed = sum(s[3] for s in entry.segments)
                    require((entry.readable_size == entry.declared_size
                             and packed == entry.packed_size) or
                            (policy == 'native-filtered-short-stream'
                             and entry.readable_size < entry.declared_size
                             and packed <= entry.packed_size),
                            f'{info.filename}: inconsistent entry size: {entry.name}')
            plans.append((info, member, entries))
        require(any(p[2] is not None for p in plans), 'no CxFilterFS archives')
        report['sourceSha256'] = sha256_file(source_path)
        with tempfile.TemporaryDirectory(prefix='.xp3-standardize-', dir=output_path.parent) as tmp:
            tmp = Path(tmp)
            candidate = tmp / 'candidate.zip'
            with zipfile.ZipFile(candidate, 'w', compression=zipfile.ZIP_STORED, allowZip64=True) as out:
                for info, member, entries in plans:
                    if entries is not None:
                        with (tmp / 'archive.xp3').open('w+b') as target:
                            items = convert_archive(member, entries, target, policy)
                        out.write(tmp / 'archive.xp3', info.filename, compress_type=zipfile.ZIP_STORED)
                        (tmp / 'archive.xp3').unlink()
                        report['archives'].append({'name': info.filename, 'entries': items})
                        print(f'{info.filename}: {len(items)} entries', file=sys.stderr, flush=True)
                    else:
                        digest = hashlib.sha256()
                        # Reading via ZipFile also checks the original member CRC.
                        with archive.open(info) as src, out.open(info, 'w', force_zip64=True) as dst:
                            while data := src.read(BUFFER):
                                digest.update(data)
                                dst.write(data)
                        report['copiedMembers'].append({'name': info.filename,
                                                        'sha256': digest.hexdigest(), 'size': info.file_size})
            require(sha256_file(source_path) == report['sourceSha256']
                    and source_path.stat().st_size == identity.st_size,
                    'source changed during conversion')
            report.update(output=str(output_path.resolve()), outputSize=candidate.stat().st_size,
                          outputSha256=sha256_file(candidate), sourceUnchanged=True)
            items = [e for a in report['archives'] for e in a['entries']]
            report.update(status='converted-with-anomalies'
                          if any(e['metadataCorrected'] for e in items) else 'converted',
                          verifiedEntries=sum(e['sourceIntegrityVerified'] for e in items),
                          anomalousEntries=sum(e['metadataCorrected'] for e in items))
            staged_report = tmp / 'conversion.json'
            with staged_report.open('x', encoding='utf-8') as f:
                json.dump(report, f, ensure_ascii=False, indent=2)
                f.write('\n')
                f.flush()
                os.fsync(f.fileno())
            with candidate.open('rb') as f:
                os.fsync(f.fileno())
            # Publish the audit first: a visible ZIP must always have its exception manifest.
            # No replace(): concurrent files must never be overwritten or removed.
            os.link(staged_report, report_path)
            try:
                directory = os.open(report_path.parent, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(directory)
                finally:
                    os.close(directory)
                os.link(candidate, output_path)
            except OSError:
                try:
                    published, staged = report_path.lstat(), staged_report.stat()
                    if (stat.S_ISREG(published.st_mode) and
                            (published.st_dev, published.st_ino) == (staged.st_dev, staged.st_ino)):
                        report_path.unlink()
                except OSError:
                    pass
                raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--format', required=True, choices=['cxfilterfs'])
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--inconsistent-entry-policy', default='reject',
                        choices=['reject', 'native-filtered-short-stream'])
    args = parser.parse_args()
    report = {}
    try:
        require(args.report.resolve() not in (args.source.resolve(), args.output.resolve()),
                'report must not overwrite input or output')
        require(not args.report.exists(), 'report exists; refusing to overwrite')
        require(args.report.parent.is_dir(), 'report parent must exist')
        require(args.report.parent.stat().st_dev == args.output.parent.stat().st_dev,
                'report and output must be on the same filesystem for atomic publication')
        standardize(args.source, args.output, args.report, report, args.inconsistent_entry_policy)
    except (FormatError, OSError, UnicodeError, zipfile.BadZipFile, zlib.error) as exc:
        report.update(status='failed', error=str(exc))
        print(f'conversion failed: {exc}', file=sys.stderr)
        # Failure reports are useful, but never overwrite existing files on an error path.
        if (args.report.resolve() not in (args.source.resolve(), args.output.resolve())
                and args.report.parent.is_dir() and not args.report.exists()):
            try:
                with args.report.open('x', encoding='utf-8') as f:
                    json.dump(report, f, ensure_ascii=False, indent=2)
                    f.write('\n')
            except OSError as report_error:
                print(f'could not persist failure report: {report_error}', file=sys.stderr)
        return 1
    print(json.dumps({k: v for k, v in report.items() if k not in ('archives', 'copiedMembers')},
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
