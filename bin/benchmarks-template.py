#!/usr/bin/python

from copy import copy
from math import log10
from pyparsing import CharsNotIn, Group, Optional, Suppress, Word, ZeroOrMore, alphanums, alphas, delimitedList, originalTextFor

import argparse
import contextlib
import json
import subprocess
import sys


def parse_template(template_text):
    identifier = Word(alphas, alphanums + '_')

    param = Group(identifier('name') + Suppress(':') + CharsNotIn(',)')('value'))
    param_list = Group(Suppress('(') + delimitedList(param, delim=',') + Suppress(')'))

    benchmark_id = originalTextFor(identifier + '.' + identifier + '.' + identifier)
    measurement_id = Group(benchmark_id('benchmark') + Optional(param_list('params')) + Suppress('[') + identifier('local_id') + Suppress(']'))

    macro = Group(Suppress('${') + measurement_id('measurement') + Suppress('}'))
    raw_text_block = originalTextFor(CharsNotIn('$'))

    text = ZeroOrMore(Group(raw_text_block('text') | macro('macro')))('template')

    text.leaveWhitespace()
    return text.parseString(template_text).asDict()


@contextlib.contextmanager
def open_output(filename=None):
    fh = open(filename, 'w') if filename and filename != '-' else sys.stdout

    try:
        yield fh
    finally:
        if fh is not sys.stdout:
            fh.close()


def main():
    parser = argparse.ArgumentParser(description='Joint adapters generator')
    parser.add_argument('-e', '--executable', help='Benchmarks executable file', required=True)
    parser.add_argument('-t', '--template', help='Template file', required=True)
    parser.add_argument('-o', '--output', default='-', help='Output file (use -o- for stdin)')
    parser.add_argument('-v', '--verbosity', type=int, default=1, help='Verbosity in range [0..4]')
    parser.add_argument('-c', '--count', type=int, default=1, help='Measurements count')
    args = parser.parse_args()

    with open(args.template) as template_file:
        template = parse_template(template_file.read())

        def make_measurement_key(m):
            return '{}({})'.format(m['benchmark'], ', '.join('{}:{}'.format(p['name'], p['value']) for p in m.get('params', [])))

        measurements = dict()
        for entry in template['template']:
            if 'macro' in entry:
                measurement = entry['macro']['measurement']
                measurements[make_measurement_key(measurement)] = measurement

        measurement_results = dict()
        for i, measurement_key in enumerate(sorted(measurements)):
            progress_format = '{{: >{}}}/{{}}: {{}}\n'.format(int(log10(len(measurements))) + 1)
            sys.stderr.write(progress_format.format(i + 1, len(measurements), measurement_key))
            measurement = measurements[measurement_key]
            cmd_args = ['--verbosity', str(args.verbosity), measurement['benchmark']] + ['{}:{}'.format(param['name'], param['value']) for param in measurement.get('params', [])]

            cmd = [args.executable, '--subtask', 'measureIterationsCount'] + cmd_args
            iterations_count = json.loads(subprocess.check_output(cmd))['iterations_count']

            cmd = [args.executable, '--subtask', 'invokeBenchmark', '--iterations', str(iterations_count)] + cmd_args
            value = min(json.loads(subprocess.check_output(cmd)) for j in xrange(args.count))
            measurement_results[measurement_key] = value

        with open_output(args.output) as out:
            for entry in template['template']:
                if 'text' in entry:
                    out.write(entry['text'])
                else:
                    measurement = entry['macro']['measurement']
                    result = measurement_results[make_measurement_key(measurement)]
                    result_dict = copy(result['memory'])
                    result_dict.update(result['times'])
                    value = result_dict[measurement['local_id']]
                    out_format = '{{:.{}f}}'.format(max(0, 1 - int(log10(value))))
                    out.write(out_format.format(value))


if __name__ == '__main__':
    main()
