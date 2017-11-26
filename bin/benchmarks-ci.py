#!/usr/bin/python

from collections import defaultdict, namedtuple

import argparse
import colorama
import copy
import json
import os
import subprocess
import sys
import traceback


ResultEntry = namedtuple("ResultEntry", "current, reference, error")


def eprint(msg):
    sys.stderr.write("{}\n".format(msg))


def run(executable, id, lang):
    out_json = subprocess.check_output([executable, '-j', '-c1', '-b', id, '--params', 'lang:{}'.format(lang)])
    out = json.loads(out_json)
    return out['times']['main']


class Context:
    def __init__(self):
        self.num_errors = 0

    def error(self, msg):
        self.num_errors += 1
        eprint('{fg}{msg}{rs}'.format(msg=msg, fg=colorama.Fore.RED, rs=colorama.Style.RESET_ALL))

    def warning(self,msg):
        eprint('{fg}{msg}{rs}'.format(msg=msg, fg=colorama.Fore.YELLOW, rs=colorama.Style.RESET_ALL))

    def info(self,msg):
        eprint(msg)

    def ok(self,msg):
        eprint('{fg}{msg}{rs}'.format(msg=msg, fg=colorama.Fore.GREEN, rs=colorama.Style.RESET_ALL))


def main():
    parser = argparse.ArgumentParser(description='Joint adapters generator')
    parser.add_argument('--executable', help='joint-benchmarks executable', required=True)
    parser.add_argument('--reference-executable', help='joint-benchmarks reference executable', required=True)
    parser.add_argument('--benchmarks', help='benchmarks.json file', required=True)
    parser.add_argument('--num-passes', help='number of joint-benchmarks passes required to measure performance', type=int, default=1)
    args = parser.parse_args()

    ctx = Context()

    with open(args.benchmarks) as benchmarks_file:
        benchmarks = json.loads(benchmarks_file.read())

    result = defaultdict(lambda: {})

    has_errors = False
    current_number = 1
    total_count = sum(len(ids) for ids in benchmarks.values())
    for lang in sorted(benchmarks):
        for id in benchmarks[lang]:
            ctx.info('{}/{}: {}, {}'.format(current_number, total_count, lang, id))
            current_number += 1

            try:
                current_list = []
                reference_list = []

                cmd_args = ['--subtask', 'measureIterationsCount', id, 'lang:{}'.format(lang)]
                current_iterations_count = json.loads(subprocess.check_output([args.executable] + cmd_args))['iterations_count']
                reference_iterations_count = json.loads(subprocess.check_output([args.reference_executable] + cmd_args))['iterations_count']

                for i in range(args.num_passes):
                    cmd = [args.executable, '--subtask', 'invokeBenchmark', '--iterations', str(current_iterations_count), id, 'lang:{}'.format(lang)]
                    current_list.append(json.loads(subprocess.check_output(cmd))['times']['main'])
                    cmd = [args.reference_executable, '--subtask', 'invokeBenchmark', '--iterations', str(current_iterations_count), id, 'lang:{}'.format(lang)]
                    reference_list.append(json.loads(subprocess.check_output(cmd))['times']['main'])

                _, current, reference = min(((abs(c - r), c, r) for c, r in zip(sorted(current_list), sorted(reference_list))), key=lambda (d, c, r): d)
                result[lang][id] = ResultEntry(reference=reference, current=current, error=None)
            except subprocess.CalledProcessError:
                has_errors = True
                result[lang][id] = ResultEntry(reference=None, current=None, error=traceback.format_exc())

    min_ratio, max_ratio = 1.0, 1.0
    for lang in sorted(result):
        lang_result = result[lang]
        for id in sorted(lang_result):
            entry = lang_result[id]
            if entry.error:
                ctx.error('{}(lang:{}):\n{}'.format(id, lang, entry.error))
            else:
                ratio = float(entry.current) / entry.reference
                min_ratio = min(min_ratio, ratio)
                max_ratio = max(max_ratio, ratio)

                def msg(text):
                    return '{}(lang:{}): {} {} -> {} ({:.2f})'.format(id, lang, text, entry.reference, entry.current, ratio)

                if entry.current < entry.reference * 0.8:
                    ctx.ok(msg('FASTER'))
                elif entry.current < entry.reference * 1.1:
                    ctx.info(msg('OK'))
                elif entry.current < entry.reference * 1.25:
                    ctx.warning(msg('A BIT SLOWER'))
                else:
                    ctx.error(msg('SLOWER'))

    ctx.info('min ratio: {:.2f}, max ratio: {:.2f}'.format(min_ratio, max_ratio))
    if ctx.num_errors:
        ctx.error('{} errors!'.format(ctx.num_errors))

    return 1 if ctx.num_errors else 0


if __name__ == '__main__':
    exit(main())
