import prometheus_client as prom
import os, time, re
from os import path

file_path = os.getcwd() + "/../bazel-out/k8-opt/bin"
update_interval = 30

def process_request(t):
    time.sleep(t)

if __name__ == '__main__':
    test_list = ["core", "inou/liveparse", "inou/code_gen", "inou/firrtl", "lemu", "main", "mmap_lib", "pass/compiler", "pass/mockturtle", "pass/sample", "pass/lnast_fromlg", "task"]

    # remove existing lbench.trace files
    for test_name in test_list:
        test_path = file_path + "/" + test_name
        for filename in os.listdir(test_path):
            if filename.endswith(".runfiles"):
                runfiles_path = test_path + "/" + filename
                trace_file = runfiles_path + "/livehd/lbench.trace"
                os.system('rm ' + trace_file)

    # run tests that generate lbench traces
    os.system('./run-lbench-test.sh')

    # create Gauge metrics for all test units
    g = {}
    for test_name in test_list:
        test_path = file_path + "/" + test_name
        for filename in os.listdir(test_path):
            if filename.endswith(".runfiles"):
                runfiles_path = test_path + "/" + filename
                trace_file = runfiles_path + "/livehd/lbench.trace"
                if path.exists(trace_file):
                    f = open(trace_file)
                    for line in f:
                        workload, performance = line.strip().split(" ", 1)
                        module, workload = workload.split(".", 1)
                        metrics = performance.split(":")
                        g_sub = {}
                        for metric in metrics:
                            name, value = metric.split("=")
                            metric_name = name.replace(" ", "_")
                            filename = os.path.splitext(filename)[0]
                            filename = filename.replace(".", "_")
                            key = module + "_" + workload + "_" + filename + "_" + metric_name
                            g_metric = prom.Gauge(key, name)
                            g_sub[key] = g_metric
                        key = module + "_" + workload + "_" + filename
                        g[key] = g_sub
                    f.close()

    # start http server
    prom.start_http_server(8000)

    # periodically traverse all lbench.trace files, update metrics, and run the tests again
    while True:
        for test_name in test_list:
            test_path = file_path + "/" + test_name
            for filename in os.listdir(test_path):
                if filename.endswith(".runfiles"):
                    runfiles_path = test_path + "/" + filename
                    trace_file = runfiles_path + "/livehd/lbench.trace"
                    if path.exists(trace_file):
                        f = open(trace_file)
                        for line in f:
                            workload, performance = line.strip().split(" ", 1)
                            module, workload = workload.split(".", 1)
                            metrics = performance.split(":")
                            filename = os.path.splitext(filename)[0]
                            filename = filename.replace(".", "_")
                            key_i = module + "_" + workload + "_" + filename
                            for metric in metrics:
                                name, value = metric.split('=')
                                metric_name = name.replace(" ", "_")
                                key_j = module + "_" + workload + "_" + filename + "_" + metric_name
                                g[key_i][key_j].set(value)
                        f.close()

        process_request(update_interval)

        for test_name in test_list:
            test_path = file_path + "/" + test_name
            for filename in os.listdir(test_path):
                if filename.endswith(".runfiles"):
                    runfiles_path = test_path + "/" + filename
                    trace_file = runfiles_path + "/livehd/lbench.trace"
                    os.system('rm ' + trace_file)

        os.system('./run-lbench-test.sh')
