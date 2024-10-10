#!/bin/python3
import argparse
import os
import logging
import shutil

global index
global total_duplicate
global last_diff
index = 1
total_duplicate = 0
last_diff = "0"

logging.basicConfig(
    level=logging.DEBUG,
    filename="deduplicate.log",
    filemode="w",
    format="%(asctime)s - %(name)s - %(levelname)-9s - %(filename)-8s : %(lineno)s line - %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)

parser = argparse.ArgumentParser()
parser.add_argument("input", type=str, help="Dir with duplicated IRs")


def diff(indir, index_a, index_b):
    a_mutated = os.path.join(indir, index_a, "mutated_opt.ll")
    b_mutated = os.path.join(indir, index_b, "mutated_opt.ll")

    from subprocess import PIPE, Popen

    p = Popen(
        f"llvm-diff {a_mutated} {b_mutated}",
        shell=True,
        stdout=PIPE,
        stderr=PIPE,
    )
    stdout, stderr = p.communicate()

    return len(str(stderr)) != 3


if __name__ == "__main__":
    args = parser.parse_args()

    dirnames = set(os.listdir(args.input))
    dirnames.remove("0")
    while len(dirnames) > 0:
        if str(index) in dirnames:
            dirnames.remove(str(index))
            dir_path = os.path.join(args.input, str(index))
            logging.info(f"Comparing {index} with {last_diff}")

            if not diff(args.input, str(index), last_diff):
                total_duplicate += 1
                shutil.rmtree(dir_path)
            else:
                last_diff = str(index)
        index += 1

    print("======Result======")
    print(f"Deduplicate {total_duplicate} cases")
