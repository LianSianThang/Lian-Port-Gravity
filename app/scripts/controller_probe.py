"""Pulse the port's opt-in SPARTAN_PAD_FILE test interface, then release it.

This writes a runtime test file; it never injects Windows keyboard/mouse input.
Use only with an isolated replay explicitly launched with SPARTAN_PAD_FILE.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--file', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=0.15)
    parser.add_argument('tokens', nargs='*')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1] / 'user'
    path = args.file.resolve()
    if not path.is_relative_to(root.resolve()) or path.name != 'pad_probe.txt':
        parser.error('Use pad_probe.txt inside an isolated project user profile.')
    if not 0.05 <= args.seconds <= 5:
        parser.error('A pulse must last between 0.05 and 5 seconds.')
    buttons = set('cross circle square triangle l1 r1 l2 r2 l3 r3 options '
                  'touchpad touchpad_left touchpad_right up down left right'.split())
    for token in args.tokens:
        if token in buttons:
            continue
        key, sep, value = token.partition('=')
        if not sep or key not in {'lx', 'ly', 'rx', 'ry', 'mx', 'my'}:
            parser.error(f'Unknown controller token: {token}')
        try:
            number = int(value)
        except ValueError:
            parser.error(f'Invalid controller value: {token}')
        low, high = (-4096, 4096) if key in {'mx', 'my'} else (0, 255)
        if not low <= number <= high:
            parser.error(f'Controller value out of range: {token}')
    if not path.is_file():
        parser.error('Create the neutral test file before launching the replay.')
    entry = {'utc': datetime.now(timezone.utc).isoformat(),
             'seconds': args.seconds, 'tokens': args.tokens}
    # The runtime polls every 20 ms. Always clear held inputs, including on Ctrl-C.
    try:
        path.write_text(' '.join(args.tokens), encoding='ascii')
        time.sleep(args.seconds)
    finally:
        path.write_text('', encoding='ascii')
        with path.with_suffix('.jsonl').open('a', encoding='utf-8') as log:
            log.write(json.dumps(entry) + '\n')
    print('Released controller input:', ' '.join(args.tokens) or '(neutral)')


if __name__ == '__main__':
    main()
