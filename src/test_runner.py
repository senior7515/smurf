#!/usr/bin/env python

# Copyright 2017 Alexander Gallego
#


import sys
import os
import logging
import logging.handlers
import subprocess
import argparse
import tempfile
import os.path
import json
import glob
import shutil

fmt_string = 'TESTRUNNER %(levelname)s:%(asctime)s %(filename)s:%(lineno)d] %(message)s'
logging.basicConfig(format=fmt_string)
formatter = logging.Formatter(fmt_string)
for h in logging.getLogger().handlers:
    h.setFormatter(formatter)
logger = logging.getLogger('it.test')
# Set to logging.DEBUG to see more info about tests
logger.setLevel(logging.INFO)


def generate_options():
    parser = argparse.ArgumentParser(description='run smf integration tests')
    parser.add_argument('--binary', type=str, help='binary program to run')
    parser.add_argument(
        '--directory',
        type=str,
        help='source directory of binary. needed for metadata')
    parser.add_argument(
        '--test_type',
        type=str,
        default="unit",
        help='either integration or unit. ie: --test_type unit')
    parser.add_argument(
        '--git_root',
        type=str,
        default=None,
        help='project root')
    return parser


def get_git_root():
    ret = str(
        subprocess.check_output("git rev-parse --show-toplevel", shell=True))
    if ret is None:
        log.error("Cannot get the git root")
        sys.exit(1)
    return "".join(ret.split())


def test_environ(maybe_git_root):
    e = os.environ.copy()
    git_root = maybe_git_root if maybe_git_root else get_git_root()
    e["GIT_ROOT"] = git_root
    ld_path = ""
    if e.has_key("LD_LIBRARY_PATH"):
        ld_path = e["LD_LIBRARY_PATH"]
    libs = "{}/src/third_party/lib:{}/src/third_party/lib64:{}".format(
        git_root, git_root, ld_path)
    e["LD_LIBRARY_PATH"] = libs
    e["GLOG_logtostderr"] = '1'
    e["GLOG_v"] = '1'
    e["GLOG_vmodule"] = ''
    e["GLOG_logbufsecs"] = '0'
    e["GLOG_log_dir"] = '.'
    e["GTEST_COLOR"] = 'no'
    e["GTEST_SHUFFLE"] = '1'
    e["GTEST_BREAK_ON_FAILURE"] = '1'
    e["GTEST_REPEAT"] = '1'
    return e


def run_subprocess(cmd, cfg, environ):
    logger.info("\nTest: {}\nConfig: {}".format(cmd, cfg))
    if not os.path.exists(cfg["execution_directory"]):
        raise Exception("Test directory does not exist: {}".format(
            cfg["execution_directory"]))

    os.chdir(cfg["execution_directory"])
    proc = subprocess.Popen(
        "exec %s" % cmd,
        stdout=sys.stdout,
        stderr=sys.stderr,
        env=environ,
        cwd=cfg["execution_directory"],
        shell=True)
    return_code = 0
    try:
        return_code = proc.wait()
        sys.stdout.flush()
        sys.stderr.flush()
    except Exception as e:
        logger.exception("Could not run command: % ", e)
        proc.kill()
        raise

    if return_code != 0: raise subprocess.CalledProcessError(return_code, cmd)


def set_up_test_environment(git_root, cfg):
    test_env = test_environ(git_root)
    dirpath = os.getcwd()
    if cfg.has_key("tmp_home") and not dirpath.startswith("/tmp/tmp."):
        print dirpath
        dirpath = tempfile.mkdtemp()
        logger.debug("Executing test in tmp dir %s" % dirpath)
        os.chdir(dirpath)
        test_env["HOME"] = dirpath
    if cfg.has_key("copy_files"):
        files_to_copy = cfg["copy_files"]
        if isinstance(files_to_copy, list):
            for f in files_to_copy:
                ff = cfg["source_directory"] + "/" + f
                for glob_file in glob.glob(ff):
                    shutil.copy(glob_file, dirpath)
    cfg["execution_directory"] = dirpath
    return test_env


def clean_test_resources(cfg):
    if cfg.has_key("execution_directory"):
        exec_dir = cfg["execution_directory"]
        if exec_dir.startswith("/tmp/") and not exec_dir.startswith("/tmp/tmp."):
            if cfg.has_key("remove_test_dir") and \
               cfg["remove_test_dir"] is False:
                logger.info("Skipping rm -r tmp dir: %s" % exec_dir)
            else:
                logger.debug("Removing tmp dir: %s" % exec_dir)
                shutil.rmtree(exec_dir)


def load_test_configuration(directory):
    try:
        test_cfg = directory + "/test.json"
        if os.path.isfile(test_cfg) is not True: return {}
        json_data = open(test_cfg).read()
        ret = json.loads(json_data)
        ret["source_directory"] = directory
        return ret
    except Exception as e:
        logger.exception("Could not load test configuration %s" % e)
        raise


def get_full_executable(binary, cfg):
    cmd = binary
    if cfg.has_key("args"):
        cmd = ' '.join([cmd] + cfg["args"])
    return cmd


def execute(cmd, test_env, cfg):
    run_subprocess(cmd, cfg, test_env)
    if cfg.has_key("repeat_in_same_dir") and cfg.has_key("repeat_times"):
        # substract one from the already executed test
        repeat = int(cfg["repeat_times"]) - 1
        while repeat > 0:
            logger.debug("Repeating test: %s, %s more time(s)", cmd, repeat)
            repeat = repeat - 1
            run_subprocess(cmd, cfg, test_env)


def prepare_test(options):
    cfg = load_test_configuration(options.directory)
    test_env = set_up_test_environment(options.git_root, cfg)
    cmd = get_full_executable(options.binary, cfg)
    return (cmd, test_env, cfg)


def main():
    parser = generate_options()
    options, program_options = parser.parse_known_args()

    if not options.binary:
        parser.print_help()
        raise Exception("Missing binary")
    if not options.directory:
        parser.print_help()
        raise Exception("Missing source directory")
    if not options.test_type:
        if (options.test_type is not "unit"
                or options.test_type is not "integration"):
            parser.print_help()
            raise Exception("Missing test_type ")

    (cmd, env, cfg) = prepare_test(options)
    execute(cmd, env, cfg)
    clean_test_resources(cfg)


if __name__ == '__main__':
    main()
