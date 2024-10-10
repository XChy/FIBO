#!python3
import sys
import subprocess
import os
import os.path as path
import tempfile
import queue
import argparse
import re
from concurrent.futures import ThreadPoolExecutor

ROOT_DIR = path.dirname(path.abspath(__file__))
res_dir = path.join(ROOT_DIR, "bench", "experiment1")
total_dir = path.join(ROOT_DIR, "bench", "total")

deopt_num = 6

seed_file_path = tempfile.NamedTemporaryFile(
    mode='w', delete=False, suffix='.txt').name

parser = argparse.ArgumentParser()
parser.add_argument("input", type=str, help="input filename")
parser.add_argument(
    "-o",
    "--output",
    type=str,
    help="output directory",
    required=True,
)

parser.add_argument(
    "-k",
    type=str,
    help="output directory",
    required=True,
)


def getopt(filepath, seq):
    prog = tempfile.NamedTemporaryFile(
        mode='w', delete=False, suffix='.ll')

    if len(seq) == 0:
        os.system(' ' .join(["opt", "-O3", "-S", filepath, "-o", prog.name]))
        return prog.name

    os.system(' ' .join(
        ["./phaser",
         "-s", seed_file_path,
         "-o", prog.name,
         "-passes", seq,
         filepath]))

    os.system(' ' .join(["opt", "-O3", "-S", prog.name, "-o", prog.name]))

    return prog.name


def isBetter(filepath, left, right):
    left_path = getopt(filepath, left)
    right_path = getopt(filepath, right)

    return os.system(f"./checker --all --perf {left_path} {right_path} | grep OK >/dev/null 2>&1") == 0


def ort(filepath: str, k: int, destdir: str):
    worklist = queue.Queue()

    best_seq = ""
    worklist.put(best_seq)

    while not worklist.empty():
        seq = worklist.get()
        if len(seq) > k:
            continue

        if len(seq) == k and isBetter(filepath, seq, best_seq):
            best_seq = seq

        for i in range(deopt_num):
            new_seq = seq + str(i)
            worklist.put(new_seq)

    best_path = getopt(filepath, best_seq)
    subprocess.run(["cp", best_path, f"{destdir}/ort{k}.ll"],  check=True)
    with open(path.join(destdir, f"ort{k}_seq.txt"), "w+") as f:
        f.write(best_seq)


def process_file(filename):
    filepath = path.join(total_dir, filename, "original.ll")
    os.makedirs(path.join(res_dir, filename), exist_ok=True)
    ort(filepath, 4, path.join(res_dir, filename))
    print(f"Complete {filename}")

if __name__ == "__main__":
    args = parser.parse_args()
    ort(args.input, int(args.k), args.output)
