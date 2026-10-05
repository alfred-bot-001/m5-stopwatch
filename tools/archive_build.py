"""Keep private, matching ELF symbols for every firmware image we flash."""
import hashlib
import json
from pathlib import Path


def archive_build(root: Path, build: Path, project: str) -> Path:
    elf = build / f'{project}.elf'
    files = [elf, build / f'{project}.bin', build / 'bootloader/bootloader.bin',
             build / 'partition_table/partition-table.bin']
    # Check everything before requesting a device reset.
    contents = {p.name: p.read_bytes() for p in files}
    elf_sha = hashlib.sha256(contents[elf.name]).hexdigest()
    destination = root / 'reports' / 'firmware-archive' / project / elf_sha
    destination.mkdir(parents=True, exist_ok=True, mode=0o700)
    manifest = {'project': project, 'elf_sha256': elf_sha, 'files': {}}
    for name, data in contents.items():
        target = destination / name
        target.write_bytes(data)
        target.chmod(0o600)
        manifest['files'][name] = hashlib.sha256(data).hexdigest()
    target = destination / 'manifest.json'
    target.write_text(json.dumps(manifest, indent=2) + '\n')
    target.chmod(0o600)
    return destination
