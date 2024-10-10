#!python3

import os
import os.path as path
import subprocess
import argparse
import random
import queue
import tempfile

DIR = path.dirname(path.abspath(__file__))
normalizer = path.join(path.dirname(
    path.abspath(__file__)), 'build', 'llvm-normal')
parser = argparse.ArgumentParser()

parser.add_argument("input", type=str, help="input filename")
parser.add_argument(
    "-o",
    "--output",
    type=str,
    help="output directory",
    required=True,
)


def readpasses():
    with open(path.join(DIR, "passes-noipo.txt")) as passlistfile:
        passes = passlistfile.readlines()
        passes = [passname.strip() for passname in passes]
        return passes


def readir():
    with open(args.input) as irfile:
        return irfile.read()
    print("No such file or directory: " + args.input)
    exit(-1)


def output(ir: str, seq: list):
    with open(path.join(args.output, "phased.ll"), 'w+') as irfile:
        irfile.write(ir)
    with open(path.join(args.output, "sequence.txt"), 'w+') as seqfile:
        seqfile.write(','.join(seq))



def compare(original_path: str, current_path: str):
    compare = subprocess.run([path.join(DIR, "checker"), "-all", "-perf", current_path, original_path], capture_output=True,
                             text=True, encoding='UTF-8')
    return compare.stdout == "OK\n"


def spawn_opt(ir: str, passname: str):
    opt = subprocess.Popen(["opt", "-S", "--passes", passname], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           text=True, encoding='UTF-8')
    opt.stdin.write(ir)
    opt.stdin.close()
    return opt


def spawn_normalizer(ir: str):
    opt = subprocess.Popen([normalizer], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           text=True, encoding='UTF-8')
    opt.stdin.write(ir)
    opt.stdin.close()
    return opt


def main():
    visited = set()

    best_ir = readir()
    best_seq = []

    passes = readpasses()
    worklist = queue.Queue()
    worklist.put((best_ir, []))

    best_file = tempfile.NamedTemporaryFile(
        mode='w', delete=False, suffix='.ll')
    current_file = tempfile.NamedTemporaryFile(
        mode='w', delete=False, suffix='.ll')

    best_file.truncate(0)
    best_file.write(best_ir)
    best_file.flush()

    iteration = 0
    while not worklist.empty():
        iteration += 1
        cur_ir, sequence = worklist.get()
        if cur_ir in visited:
            continue
        visited.add(cur_ir)

        current_file.truncate(0)
        current_file.write(cur_ir)
        current_file.flush()

        if compare(best_file.name, current_file.name):
            best_ir = cur_ir
            best_seq = sequence
            best_file.truncate(0)
            best_file.write(best_ir)
            best_file.flush()
            print(f"{iteration}th iteration!")
            print(best_seq)
            print(best_ir)
        # else:
            # print("No:")
            # print(sequence)
            # print(best_seq)

        opt_processes = [spawn_opt(cur_ir, passname)
                         for passname in passes]
        opt_children = []
        for process in opt_processes:
            process.wait()
            opt_children += [''.join(process.stdout.readlines())]

        normalizer_processes = [spawn_normalizer(opt_child)
                                for opt_child in opt_children]

        i = 0
        for process in normalizer_processes:
            process.wait()
            child_ir = ''.join(process.stdout.readlines())
            worklist.put((child_ir, sequence + [passes[i]]))
            i += 1

        print(f"{iteration}th iteration! {len(visited)} distinct")

    best_file.close()
    current_file.close()
    print("Get best seq")
    print(best_seq)
    print("Get best:")
    print(best_ir)

    output(best_ir, best_seq)


if __name__ == '__main__':
    args = parser.parse_args()
    main()
