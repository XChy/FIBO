#!python3
import toml
import os
import os.path as path
import sys
import argparse
import tempfile
import subprocess
import logging

global unoptgen
root_dir = path.split(path.split(__file__)[0])[0]
unoptgen_build = path.join(root_dir, "build")
unoptgen = path.join(root_dir, "build", "unoptgen")
moclassify = path.join(root_dir, "build", "moclassify")

global args
global index_missed_better
global index_crashed
index_missed_better = 0
index_crashed = 0

logging.basicConfig(
    level=logging.DEBUG,
    filename="run.log",
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

parser.add_argument(
    "-c",
    "--config",
    type=str,
    help="config file",
    required=True,
)


def mutate(original_path, mutant_path, working_dir, config):
    success = os.system(
        "{0} -m {1} {2} -o {3}/mutated.ll -p {3}/pipeline -s {3}/seed -pipeline-type={4}"
        .format(unoptgen, config["mutate"]["max_passes"], original_path, working_dir, config["mutate"]["type"]))
    return success


def mine(original_path, config_path):
    working_dir = tempfile.mkdtemp()
    default_config_path = path.join(root_dir, "config", "default.toml")
    config = toml.load([default_config_path, config_path])

    global index_crashed
    # Mutate IR
    os.system(f"cp {original_path} {working_dir}/original.ll")
    os.system(f"cp {config_path} {working_dir}/config.toml")

    functions = None

    for pipeline in config["pipeline"]:
        ret = subprocess.run(
            [unoptgen, "-m", str(config["mutate"]["max_passes"]),
             "-o", path.join(working_dir,
                             pipeline["mutant_name"]),
             "-p", path.join(
                working_dir, "pipeline"), "-s", path.join(working_dir, "seed"),
             "-pipeline-type", str(config["mutate"]["type"]),
             "-remove-last", str(pipeline["remove_last"]),
             original_path
             ])

        if ret.returncode != 0:
            logging.error(f"UnoptGen crashed when mutating {original_path}")
            crash_dir = path.join(
                args.output, "crashed", str(index_crashed))
            os.system(f"mv {working_dir} {crash_dir}")
            logging.error(f"Mutated {original_path} crashed opt")
            index_crashed += 1
            return

        ret = subprocess.run(
            [
                "opt", "-S", config["optimize"]["mutant"],
                path.join(working_dir, pipeline["mutant_name"]),
                "-o", path.join(working_dir,
                                pipeline["optimized_mutant_name"]),
            ])

        if ret.returncode != 0:
            crash_dir = path.join(
                args.output, "crashed", str(index_crashed))
            os.system(f"mv {working_dir} {crash_dir}")
            logging.error(f"Mutated {original_path} crashed opt")
            index_crashed += 1
            return

        ret = subprocess.run(
            [
                "opt", "-S", config["optimize"]["original"],
                path.join(working_dir, "original.ll"),
                "-o", path.join(working_dir,
                                config["optimized_original_name"])
            ])
        if ret.returncode != 0:
            crash_dir = path.join(
                args.output, "crashed", str(index_crashed))
            os.system(f"mv {working_dir} {crash_dir}")
            logging.error(f"Original {original_path} crashed opt")
            index_crashed += 1
            return

        p = subprocess.Popen(
            [moclassify,
             path.join(working_dir, pipeline["optimized_mutant_name"]),
             path.join(working_dir, config["optimized_original_name"]),
             ] + (["-reverse"] if pipeline["reverse_check"] else []),
            stdout=subprocess.PIPE,
            encoding='utf-8')

        out = str(p.communicate()[0])

        print("===", pipeline["name"], "===")
        print(out)
        if functions is None:
            functions = set(out.splitlines(False))
        else:
            functions = functions.intersection(out.splitlines(False))

    if functions is None or len(functions) == 0:
        return

    global index_missed_better
    missed_dir = path.join(
        args.output, "missed-opt", str(index_missed_better))
    os.system(f"mv {working_dir} {missed_dir}")
    os.system(f"echo {original_path} > {missed_dir}/source_path.txt")
    index_missed_better += 1

    i = 0
    for func in functions:
        func_path = path.join(missed_dir, str(i))
        os.system(f"mkdir -p {func_path}")
        os.system(f"echo {func} > {func_path}/func_name")
        subprocess.run(
            ["llvm-extract",
             original_path,
             "-S",
             "--func", func,
             "-o", path.join(func_path, config["optimized_original_name"])])
        for pipeline in config["pipeline"]:
            subprocess.run(
                ["llvm-extract",
                 path.join(missed_dir, pipeline["optimized_mutant_name"]),
                 "-S",
                 "--func", func,
                 "-o", path.join(func_path, pipeline["optimized_mutant_name"])])

        i += 1


if __name__ == "__main__":
    args = parser.parse_args()

    os.makedirs(path.join(args.output, "missed-opt"), exist_ok=True)
    os.makedirs(path.join(args.output, "crashed"), exist_ok=True)

    os.system(f"cp {args.config} {args.output}/config.toml")

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
                    mine(path.join(root, filename), args.config)
                    cur_checks += 1
                    print(
                        f"\rProgress: [{cur_checks}/{total_checks}],  Found: {index_missed_better}")
                    sys.stdout.flush()

    print("=======Result=======")
    print(f"Found {index_missed_better} candidates")
    print(f"Found {index_crashed} crashed cases")
