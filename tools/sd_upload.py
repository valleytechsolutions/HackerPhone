"""Upload a new file to /hackerphone over USB, with whole-file CRC and atomic commit."""
import argparse, pathlib, zlib
from phone_usb import PhoneLink

def upload(link, source, destination):
    if not destination.startswith('/hackerphone/') or len(destination) > 140:
        raise ValueError('Destination must be under /hackerphone and at most 140 characters')
    size = source.stat().st_size
    crc = 0
    with source.open('rb') as f:
        for block in iter(lambda: f.read(65536), b''):
            crc = zlib.crc32(block, crc)
    def command(text):
        result = link.command(text, b'HP_FILE ', timeout=10)
        if not result.startswith(b'HP_FILE OK '):
            raise RuntimeError(result.decode())
    command(f'UPLOAD BEGIN {size} {crc:08x} {destination}')
    try:
        offset = 0
        with source.open('rb') as f:
            for block in iter(lambda: f.read(96), b''):
                command(f'UPLOAD DATA {offset} {block.hex()}')
                offset += len(block)
        command('UPLOAD END')
    except Exception:
        link.command('FILE CANCEL', b'HP_FILE ')
        raise
    return size, crc

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('destination', help='New absolute SD path, e.g. /hackerphone/music/track.mp3')
    args = parser.parse_args()
    link = PhoneLink(args.port)
    try:
        size, crc = upload(link, args.source, args.destination)
        print(f'Saved {size} bytes; CRC32 {crc:08x}; existing files were not overwritten.')
    finally:
        link.close()
