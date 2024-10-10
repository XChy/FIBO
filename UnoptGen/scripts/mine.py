#!python3
import argparse
import os
import sys
import tempfile
import logging

global unoptgen
global moconfirm
unoptgen_build = "~/Projects/UnoptGen/build/"
unoptgen = unoptgen_build + "unoptgen"
moclassify = unoptgen_build + "moclassify"
global args

global index_missed_better
global index_missed_worse
global index_missed_diff
global index_crashed
index_missed_better = 0
index_missed_worse = 0
index_missed_diff = 0
index_crashed = 0

logging.basicConfig(
    level=logging.DEBUG,
    filename="unoptgen.log",
    filemode="w",
    format="%(asctime)s - %(name)s - %(levelname)-9s - %(filename)-8s : %(lineno)s line - %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)

parser = argparse.ArgumentParser()
parser.add_argument("input", type=str, help="Input dir for llvmirs")

parser.add_argument(
    "-o",
    "--output",
    type=str,
    help="dir to output the results",
    required=True,
)


def mine(original_path):
    working_dir = tempfile.mkdtemp()

    global index_crashed
    # Mutate IR
    os.system(f"cp {original_path} {working_dir}/original.ll")
    success = os.system(
        f"{unoptgen} -m 16 {original_path} -o {working_dir}/mutated.ll -p {working_dir}/pipeline -s {working_dir}/seed")
    if success != 0:
        logging.error(f"UnoptGen crashed when mutating {original_path}")
        crash_dir = os.path.join(args.output, "crashed", str(index_crashed))
        os.system(f"mv {working_dir} {crash_dir}")
        logging.error(f"Mutated {original_path} crashed opt")
        index_crashed += 1
        return

    success = os.system(
        f"opt -S -O3 {working_dir}/mutated.ll -o {working_dir}/mutated_opt.ll")
    if success != 0:
        crash_dir = os.path.join(args.output, "crashed", str(index_crashed))
        os.system(f"mv {working_dir} {crash_dir}")
        logging.error(f"Mutated {original_path} crashed opt")
        index_crashed += 1
        return

    success = os.system(
        f"opt -S -O3 {original_path} -o {working_dir}/original_opt.ll")
    if success != 0:
        logging.error(f"{original_path} crashed opt")
        return

    success = os.system(
        f"{moclassify} {working_dir}/mutated_opt.ll {working_dir}/original_opt.ll --output {working_dir}")

    if success != 0:
        return

    global index_missed_better
    missed_dir = os.path.join(
        args.output, "missed-opt", str(index_missed_better))
    os.system(f"mv {working_dir} {missed_dir}")
    os.system(f"echo {original_path} > {missed_dir}/source_path.txt")

    index_missed_better += 1


if __name__ == "__main__":
    args = parser.parse_args()

    os.makedirs(os.path.join(args.output, "missed-opt"), exist_ok=True)
    os.makedirs(os.path.join(args.output, "crashed"), exist_ok=True)

    check_per_file = 3
    total_checks = 0
    cur_checks = 0

    for root, _, filenames in os.walk(args.input):
        for filename in filenames:
            if filename.endswith(".ll"):
                total_checks += check_per_file

    for root, _, filenames in os.walk(args.input):
        for filename in filenames:
            for i in range(check_per_file):
                if filename.endswith(".ll"):
                    mine(os.path.join(root, filename))
                    cur_checks += 1
                    print(
                        f"\rProgress: [{cur_checks}/{total_checks}],  Found: {index_missed_better}")
                    sys.stdout.flush()

    print("=======Result=======")
    print(f"Found {index_missed_better} better cases")
    print(f"Found {index_crashed} crashed cases")
