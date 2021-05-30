import json
import logging
import os
from pathlib import Path
from subprocess import check_output
from typing import Callable, List, Optional

import opentuner as ot
from tqdm import tqdm
from opentuner.search.manipulator import ConfigurationManipulator

# fmt: off
llvm_build_dir = Path(__file__).parent / "../build"
OPT = llvm_build_dir / "bin/opt"
CLANG = llvm_build_dir / "bin/clang"
OPT_FLAGS = """-targetlibinfo -tti -tbaa -scoped-noalias-aa -assumption-cache-tracker -profile-summary-info
-forceattrs -inferattrs -ipsccp -called-value-propagation -globalopt -domtree -mem2reg -deadargelim -basic-aa -aa
-loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -simplifycfg -basiccg -globals-aa
-prune-eh -inline -function-attrs -memoryssa -early-cse-memssa -speculative-execution
-lazy-value-info -jump-threading -correlated-propagation -libcalls-shrinkwrap -branch-prob -block-freq
-pgo-memop-opt -tailcallelim -reassociate -loop-simplify -lcssa-verification -lcssa -scalar-evolution
-loop-rotate -licm -loop-unswitch -loop-idiom -memdep -memcpyopt -sccp
-demanded-bits -bdce -dse -postdomtree -barrier -rpo-function-attrs -globaldce -float2int -loop-accesses
-loop-distribute -loop-vectorize -loop-load-elim -alignment-from-assumptions -strip-dead-prototypes -loop-sink
-instsimplify -div-rem-pairs -verify -ee-instrument -early-cse -lower-expect
-inline -mldst-motion -gvn -elim-avail-extern -slp-vectorizer -constmerge
""".replace("\n", " ").strip()
# https://stackoverflow.com/questions/15548023/clang-optimization-levels
# Unused optimizations compared to -O2:  -indvars -loop-deletion -loop-unroll -sroa -instcombine
logger = logging.getLogger(__name__)
# fmt: on


def parse_args():
    parser = ot.default_argparser()
    parser.add_argument(
        "--debug", action="store_true", help="Enable output from LLVM passes"
    )
    parser.add_argument(
        "-d", "--workdir", type=Path, default=Path("."), help="Working directory"
    )
    parser.add_argument("-r", "--repeats", type=int, default=3)
    parser.add_argument("--defs", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("-p", "--partition", action="store_true")
    parser.add_argument("code_file", type=Path)
    args = parser.parse_args()
    if not args.output:
        args.output = args.workdir / "./configs.json"
    return args


def main():
    args = parse_args()
    ot.tuningrunmain.init_logging()
    tc = TargetCharacter(
        args.code_file, args.workdir, args.debug, args.defs, args.repeats
    )
    configs = ConfigAccumulator()

    def tune_once(knobs):
        interface = MeaInterface(args, tc, knobs)
        trm = ot.tuningrunmain.TuningRunMain(interface, args)
        # A little bit of hack to get the _real_ progress when duplicated configs exist
        interface.set_progress_getter(lambda: trm.search_driver.test_count)
        logger.info(
            "Estimated size of search space: %d", trm.manipulator.search_space_size()
        )
        # This is where opentuner runs
        trm.main()
        # Process configs
        configs.extend(interface.configs)
        best = interface.best_config
        assert best is not None
        tc.add_best_config_part(best)
        return interface

    if args.partition:
        masks = tc.list_masks()
        logger.info("Partitioning tuning space -- %d operators found", len(masks))
        logger.info("%s", masks)
        for mask in masks:
            tune_once(tc.filter_knobs(f"{mask}."))
    else:
        tune_once(tc.dsl_knobs)
    configs.write(args.output)


class ConfigAccumulator:
    def __init__(self) -> None:
        self.configs = []
        self.max_iter = 0

    def extend(self, configs: List[dict]):
        local_max = 0
        for config in configs:
            config["iter"] += self.max_iter
            local_max = max(local_max, config["iter"])
        self.max_iter = local_max + 2
        self.configs.extend(configs)

    def write(self, filename: Path):
        with filename.open("w") as f:
            json.dump(self.configs, f, indent=2)


class TargetCharacter:
    def __init__(
        self,
        code_file: Path,
        workdir: Path,
        debug: bool,
        defs: Optional[Path],
        repeats: int,
    ) -> None:
        self.code_file = code_file
        self.debug = debug
        self.workdir = workdir
        self.repeats = repeats
        os.makedirs(self.workdir, exist_ok=True)

        self.defs = []
        if defs is not None:
            self.defs = self._read_defs(defs)
            logger.info("Using extra definitions: %s", self.defs)
        self.dsl_knobs = self._get_knobs_for_dsl(code_file, self.defs)
        # Start with default values
        self.best_config_parts = {
            op: knob["data_range"][0] if "data_range" in knob else min(knob["values"])
            for op, knob in self.dsl_knobs.items()
        }
        logger.info("Baseline config: %s", self.best_config_parts)
        self.baseline = self.compile_run_measure(self.best_config_parts)
        logger.info("Baseline time: %.3f", self.baseline)

    def list_masks(self) -> List[str]:
        operators = set()
        for knob_name in self.dsl_knobs:
            func, op, _ = knob_name.split(".")
            operators.add(f"{func}.{op}")
        return list(operators)

    def filter_knobs(self, op_mask: str):
        return {k: v for k, v in self.dsl_knobs.items() if op_mask in k}

    def add_best_config_part(self, config: dict):
        self.best_config_parts.update(config)

    def compile_run_measure(self, config: dict):
        from time import time
        from subprocess import CalledProcessError

        binpath = self._compile_with_knobs(config)
        time0 = time()
        bin_str = str(binpath) if binpath.is_absolute() else f"./{binpath}"
        try:
            for _ in range(self.repeats):
                check_output([bin_str])
        except CalledProcessError as e:
            logger.warning("Measured config %s died with error: %s", config, e)
            return float("inf")
        return (time() - time0) / self.repeats

    def _read_defs(self, path: Path):
        with path.open() as f:
            data = json.load(f)
        defs = []
        for k, v in data.items():
            if not isinstance(v, (int, str)):
                raise ValueError(
                    f"Only int and string are supported to pass as -D defs"
                )
            defs.append(f"-D{k}={v}")
        return defs

    def _get_knobs_for_dsl(self, filename: Path, extra_defs: List[str]):
        prefix = self.workdir / filename.stem
        rjson = prefix.with_suffix(".json")
        rjson.unlink(missing_ok=True)
        clang_flags = "-S -emit-llvm -O0 -Xclang -disable-O0-optnone -mavx512f"
        lower_flags = f"-load LLVMTensor.so -mem2reg -tensor -tensor-analysis -lower-tensor -print-knobs-to {rjson}"
        self._pipeline(
            prefix,
            filename,
            {
                ".ll": [CLANG, clang_flags, *extra_defs],
                ".labeled.ll": [OPT, lower_flags],
            },
        )
        with rjson.open() as f:
            data = json.load(f)
        ret = flatten_dicts(data, 3)
        with rjson.open("w") as f:
            json.dump(ret, f, indent=2)
        return ret

    def _compile_with_knobs(self, knobs: dict):
        filled_knobs = {**self.best_config_parts, **knobs}
        wjson = self.workdir / "tmp.json"
        with wjson.open("w") as f:
            json.dump(unflatten_dict(filled_knobs), f)
        output_bin: Path = self.workdir / self.code_file.stem
        lower_cmd = f"-load LLVMTensor.so -tensor-analysis -lower-tensor -read-knobs-from {wjson}"
        self._pipeline(
            output_bin,
            output_bin.with_suffix(".labeled.ll"),
            {
                ".lowered.ll": [OPT, lower_cmd],
                ".lowered.opt.ll": [OPT, OPT_FLAGS],
                "": [CLANG],
            },
        )
        with output_bin.with_suffix(".lowered.opt.ll").open() as f:
            if f.read().find(" mul") == -1:
                raise RuntimeError(
                    f"Abnormal optimization result. Config: {filled_knobs}"
                )
        return output_bin

    def _pipeline(self, prefix: Path, first_input: Path, kwargs):
        last_file = first_input
        for suffix, commands in kwargs.items():
            next_file = prefix.with_suffix(suffix)
            cmds = [commands[0], *commands[1:], last_file, f"-o {next_file}"]
            if commands[0] == OPT:
                cmds += ["-S"]
            self._split_print_and_run(cmds)
            last_file = next_file

    def _split_print_and_run(self, commands, *args, **kwargs):
        from subprocess import STDOUT, check_call

        split_commands = []
        for cmd in commands:
            split_commands.extend(str(cmd).split(" "))
        logger.debug(" ".join(split_commands))
        if self.debug:
            check_call(split_commands, *args, **kwargs)
        else:
            check_output(split_commands, *args, stderr=STDOUT, **kwargs)


class MeaInterface(ot.MeasurementInterface):
    def __init__(self, args, targetc: TargetCharacter, knobs: dict):
        from opentuner.measurement.inputmanager import FixedInputManager
        from opentuner.search.objective import MinimizeTime

        super().__init__(
            args=args,
            objective=MinimizeTime(),
            input_manager=FixedInputManager(len(knobs)),
        )
        self.knobs = knobs
        self.tc = targetc

        self.pbar = tqdm(total=args.test_limit, leave=False)
        self.progress_getter = None
        self.configs = []
        self.best_config = None
        self.confs_measured = 0  # Different from numbers of iteration
        logger.info("%d knobs", len(self.knobs))
        logger.debug("Knobs: %s", self.knobs)

    def manipulator(self) -> ConfigurationManipulator:
        from opentuner.search.manipulator import EnumParameter, IntegerParameter

        manipulator = ConfigurationManipulator()
        for op, knob_traits in self.knobs.items():
            dt = knob_traits["data_type"]
            if dt != "int":
                raise TypeError(f"Non-integer {dt} knob type is unsupported")
            if "data_range" in knob_traits:
                lhs, rhs = knob_traits["data_range"]
                manipulator.add_parameter(IntegerParameter(op, lhs, rhs))
            else:
                values = sorted(knob_traits["values"])
                manipulator.add_parameter(EnumParameter(op, values))
        return manipulator

    def run(self, desired_result, input_, limit):
        """Run a given configuration then return cost and QoS."""
        from opentuner.resultsdb.models import Result

        cfg = desired_result.configuration.data
        time = self.tc.compile_run_measure(cfg)
        logger.debug("Config: %s, time: %.3f", cfg, time)

        speedup = self.tc.baseline / time
        self.configs.append(
            {**cfg, "time": time, "iter": self.confs_measured, "speedup": speedup}
        )
        self.pbar.update(self.progress_getter() - self.pbar.n)
        self.confs_measured += 1
        return Result(time=time)

    def save_final_config(self, config):
        logger.info("Final config: %s", config.data)
        self.best_config = config.data
        self.pbar.close()

    def set_progress_getter(self, getter: Callable[[], int]):
        self.progress_getter = getter


def flatten_dicts(ddd: dict, n_levels: int, prefix: str = "") -> dict:
    if n_levels == 0:
        return {prefix: ddd}
    ret = {}
    prefix = f"{prefix}." if prefix else ""
    for k, v in ddd.items():
        ret.update(flatten_dicts(v, n_levels - 1, f"{prefix}{k}"))
    return ret


def unflatten_dict(d: dict) -> dict:
    from collections import defaultdict

    if not isinstance(d, dict):
        return d
    ret = defaultdict(dict)
    for k, v in d.items():
        if "." in k:
            first, rest = k.split(".", 1)
            ret[first][rest] = v
        else:
            ret[k] = v
    for k, v in ret.items():
        ret[k] = unflatten_dict(v)
    return ret


if __name__ == "__main__":
    main()
