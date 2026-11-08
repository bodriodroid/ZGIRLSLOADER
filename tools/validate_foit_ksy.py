#!/usr/bin/env python3
"""Checks docs/foit.ksy against real .foit files.

For every file the Kaitai Struct parser generated from docs/foit.ksy must
  1. consume the file exactly (last byte reached), and
  2. produce the same structural totals as the hand written reader in src/core/foit.cpp
     (`zdroid_foit --stats`: objects, states, nodes, props, chains, chain nodes, hit data, commands, steps,
     transitions, frames, images, boxes ...), and
  3. agree with `zdroid_foit --states` on every state of the character (name, node count, ticks, next state,
     transition count).

The .foit files are NOT part of this repository: point the script at your own copy of the PC build's characters.

  # 1) generate the parser (either of the two works)
  kaitai-struct-compiler -t python --outdir /tmp/foit_py docs/foit.ksy
  #    or, with the npm package (npm i kaitai-struct-compiler) and PyYAML:
  python3 tools/validate_foit_ksy.py --compile /tmp/foit_py --ksc-node-modules ./node_modules

  # 2) run the check (needs `pip install kaitaistruct` and a built zdroid_foit)
  python3 tools/validate_foit_ksy.py --parser /tmp/foit_py --tool build/zdroid_foit  <folder or .foit files>
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KSY = os.path.join(HERE, '..', 'docs', 'foit.ksy')

NODE_COMPILE = r"""
const c = require(process.argv[2] + '/kaitai-struct-compiler');
const fs = require('fs');
const ksy = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const importer = { importYaml: (n) => Promise.reject(new Error('no imports: ' + n)) };
Promise.resolve(c.compile('python', ksy, importer, false)).then(files => {
  for (const [name, content] of Object.entries(files)) fs.writeFileSync(process.argv[4] + '/' + name, content);
}).catch(e => { console.error('compile error:', e && (e.s || e.message || e)); process.exit(1); });
"""


def compile_ksy(outdir, node_modules):
    os.makedirs(outdir, exist_ok=True)
    ksc = shutil.which('kaitai-struct-compiler') or shutil.which('ksc')
    if ksc:
        subprocess.check_call([ksc, '-t', 'python', '--outdir', outdir, KSY])
        return
    import yaml  # PyYAML
    tmp = os.path.join(outdir, '_foit.json')
    with open(KSY) as f, open(tmp, 'w') as g:
        json.dump(yaml.safe_load(f), g)
    js = os.path.join(outdir, '_compile.js')
    with open(js, 'w') as f:
        f.write(NODE_COMPILE)
    subprocess.check_call(['node', js, os.path.abspath(node_modules), tmp, outdir])
    os.remove(tmp)
    os.remove(js)


# ---------------------------------------------------------------------------------------------------------------
def opt(o, name):
    """Optional (`if:`) fields of the generated parser are simply absent when the condition is false."""
    return getattr(o, name, None)


class Totals:
    KEYS = ['objects', 'states', 'nodes', 'script_nodes', 'props', 'chains', 'chain_nodes', 'hits', 'subs', 'commands',
            'steps', 'transitions', 'u32s', 'hashes', 'frames', 'images', 'hit_boxes', 'hurt_boxes', 'push_boxes',
            'entries', 'list8']

    def __init__(self):
        for k in self.KEYS:
            setattr(self, k, 0)


def t_chain(t, c):
    if c is not None:
        t.chains += 1
        t.chain_nodes += len(c.nodes)


def t_cmd(t, c):
    t.commands += 1
    if opt(c, 'default_step') is not None:
        t.steps += 1
        t_chain(t, opt(c.default_step, 'condition'))
    for st in c.steps:
        t.steps += 1
        t_chain(t, opt(st, 'condition'))


def t_props(t, lst):
    for p in lst.items:
        t.props += 1
        t_chain(t, opt(p, 'chain'))
        hit = opt(p, 'hit')
        if hit is not None:
            t.hits += 1
            for k in range(4):
                t_chain(t, opt(hit, 'chain%d' % k))
        sub = opt(p, 'sub')
        if sub is not None:
            for si in sub.items:
                t.subs += 1
                t_chain(t, opt(si, 'chain'))
        if opt(p, 'u32s'):
            t.u32s += len(p.u32s)
        if opt(p, 'command') is not None:
            t_cmd(t, p.command)


def t_object(t, o):
    t.objects += 1
    for k in range(3):
        t_chain(t, opt(o.tail, 'chain%d' % k))
    for st in o.states.states:
        t.states += 1
        if opt(st, 'hashes'):
            t.hashes += len(st.hashes)
        h = st.header
        for nd in h.nodes:
            t.nodes += 1
            t.script_nodes += (nd.ref0 == 0 and nd.ref1 == 0 and nd.ref2 == 0)
            t_props(t, nd.props)
        if opt(h, 'command') is not None:
            t_cmd(t, h.command)
            t_props(t, h.command_props)
        t_chain(t, opt(h, 'chain'))
        for tr in st.transitions:
            t.transitions += 1
            t_cmd(t, tr.command)
    for ch in o.children:
        t_object(t, ch.body)


def totals(foit):
    t = Totals()
    b = foit.body
    t_object(t, b)
    t.list8 = b.tail.num_list8
    if opt(b, 'frame_list') is not None:
        for f in b.frame_list.frames:
            t.frames += 1
            t.images += sum(1 for k in range(8) if opt(f, 'image%d' % k) is not None)
            t.hit_boxes += len(f.hit_boxes.boxes)
            t.hurt_boxes += len(f.hurt_boxes.boxes)
            t.push_boxes += len(f.push_boxes.boxes)
            t.entries += len(f.entries)
    return t


def state_rows(o):
    """(index, name, nodes, ticks, next name, transitions) for every state of the object."""
    names = [n.value for n in o.state_names.names]
    rows = []
    for i, st in enumerate(o.states.states):
        ticks = 0
        for nd in st.header.nodes:
            if nd.ref0 or nd.ref1 or nd.ref2:
                p = nd.props.items[0]
                ticks += p.duration if (p.b1 & 1) else 1
        nxt = names[st.next] if st.next < len(names) else '?%d' % st.next
        rows.append((i, names[i], len(st.header.nodes), ticks, nxt, st.num_transitions))
    return rows


STATE_RE = re.compile(r'^  state\s*(\d+) (.*?)\s+nodes\s+(\d+) ticks\s+(-?\d+) next (.*?)\s+transitions (\d+)\s*$')


def tool_rows(tool, path):
    out = subprocess.run([tool, path, '--states'], capture_output=True, text=True).stdout
    rows = []
    for ln in out.splitlines():
        m = STATE_RE.match(ln)
        if m:
            rows.append((int(m.group(1)), m.group(2), int(m.group(3)), int(m.group(4)), m.group(5), int(m.group(6))))
    return rows


def tool_stats(tool, path):
    out = subprocess.run([tool, path, '--stats'], capture_output=True, text=True).stdout
    for ln in out.splitlines():
        if ln.startswith('stats '):
            parts = ln.split()
            return {k: int(v) for k, v in (p.split('=') for p in parts[2:])}
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('inputs', nargs='*', help='.foit files or folders')
    ap.add_argument('--parser', help='folder with the generated foit.py')
    ap.add_argument('--compile', metavar='OUTDIR', help='generate the parser into OUTDIR first')
    ap.add_argument('--ksc-node-modules', default='node_modules', help='npm node_modules folder with kaitai-struct-compiler')
    ap.add_argument('--tool', default='zdroid_foit', help='path to the zdroid_foit tool')
    args = ap.parse_args()

    if args.compile:
        compile_ksy(args.compile, args.ksc_node_modules)
        args.parser = args.parser or args.compile
        if not args.inputs:
            print('parser written to', args.compile)
            return 0
    if not args.parser or not args.inputs:
        ap.print_usage()
        return 2
    sys.path.insert(0, args.parser)
    from foit import Foit  # noqa: E402

    files = []
    for i in args.inputs:
        files += sorted(glob.glob(os.path.join(i, '**', '*.foit'), recursive=True)) if os.path.isdir(i) else [i]

    bad = 0
    for path in files:
        name = os.path.basename(path)[:-5]
        size = os.path.getsize(path)
        try:
            foit = Foit.from_file(path)
        except Exception as e:  # noqa: BLE001
            print('%-12s FAIL parse: %r' % (name, e))
            bad += 1
            continue
        problems = []
        if foit._io.pos() != size:
            problems.append('stopped at %d of %d bytes' % (foit._io.pos(), size))
        mine = totals(foit)
        ref = tool_stats(args.tool, path)
        if ref is None:
            problems.append('zdroid_foit --stats gave nothing')
        else:
            for k in Totals.KEYS:
                if ref.get(k) != getattr(mine, k):
                    problems.append('%s: ksy %s vs zdroid_foit %s' % (k, getattr(mine, k), ref.get(k)))
        a, b = state_rows(foit.body), tool_rows(args.tool, path)
        if len(a) != len(b):
            problems.append('state rows: ksy %d vs zdroid_foit %d' % (len(a), len(b)))
        else:
            diff = [(x, y) for x, y in zip(a, b) if x != y]
            if diff:
                problems.append('%d state row(s) differ, first: ksy %s vs tool %s' % (len(diff), diff[0][0], diff[0][1]))
        lay = opt(foit.body, 'layer')
        extra = ''
        if lay is not None:
            extra = ' | palettes %d x %d colours in %d groups' % (lay.num_layers, lay.total_colors, lay.num_groups)
        if problems:
            bad += 1
            print('%-12s MISMATCH %s' % (name, '; '.join(problems[:6])))
        else:
            print('%-12s ok  %d bytes, %d objects, %d states, %d chain nodes, %d frames%s' % (
                name, size, mine.objects, mine.states, mine.chain_nodes, mine.frames, extra))
    print('\n%d of %d file(s) match' % (len(files) - bad, len(files)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
