"""Build explicit spike sources with the permitted portable compiler; no CMake dependency."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time

FILES = {
 "A": (["core/rendering/src/" + f for f in ["types.cpp","camera.cpp","culling.cpp","fixtures.cpp","trace.cpp"]] + ["core/rendering/tests/test_main.cpp"], ["core/rendering/include"], []),
 "B": (["core/geometry/src/" + f for f in ["transform.cpp","rect.cpp","curve.cpp","boolean_offset.cpp"]] + ["core/document/src/" + f for f in ["snapshot.cpp","document.cpp","persistence.cpp"]] + ["core/document/tests/" + f for f in ["test_main.cpp","test_geometry.cpp","test_document.cpp","test_persistence.cpp","test_trace_10k.cpp","test_review_regressions.cpp"]], ["core/geometry/include","core/document/include","core/document/tests"], []),
 "C": (["modules/interop/src/" + f for f in ["interop_types.cpp","ipc_dto.cpp","payload_validator.cpp","format_capability_matrix.cpp","fixture_manifest.cpp"]] + ["platform/workers/src/" + f for f in ["win32_common.cpp","job_object.cpp","appcontainer.cpp","process_launcher.cpp"]] + ["platform/workers/tests/spike_c_tests_main.cpp"], ["modules/interop/include","platform/workers/include"], ["-luserenv","-ladvapi32","-lws2_32"]),
}

def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    ap=argparse.ArgumentParser()
    ap.add_argument("--lane",choices=FILES,required=True)
    ap.add_argument("--source-root",type=pathlib.Path,required=True)
    ap.add_argument("--toolchain-root",type=pathlib.Path,required=True)
    ap.add_argument("--mode",choices=["build","test","all"],default="all")
    args=ap.parse_args()
    root=args.source_root.resolve()
    # Explicit path avoids Git trust/configuration and global environment changes.
    cache=args.toolchain_root.resolve()/"bin"
    compiler=cache/"clang++.exe"
    out=root/("out/spikes/"+args.lane.lower()+"-direct")
    out.mkdir(parents=True,exist_ok=True)
    binary=out/"spike_tests.exe"
    sources,includes,libs=FILES[args.lane]
    missing=[p for p in sources if not (root/p).is_file()]
    if missing:
        ap.error("Missing required sources: " + ", ".join(missing))
    inputs=set(sources)
    for directory in includes:
        inputs.update(p.relative_to(root).as_posix() for p in (root/directory).rglob("*.hpp"))
    if args.lane=="C": inputs.add("platform/workers/src/worker_main.cpp")
    def fingerprint():
        return {p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in sorted(inputs)}
    manifest=fingerprint()
    report={"lane":args.lane,"source_status":"uncommitted review draft","sources":manifest,"configuration":"Debug direct Clang C++20","full_spike_accepted":False}
    if args.mode=="test":
        previous=json.loads((out/"report.json").read_text(encoding="utf-8"))
        if previous.get("sources")!=manifest or previous.get("binary_sha256")!=hashlib.sha256(binary.read_bytes()).hexdigest():
            raise RuntimeError("Test binary does not match these sources; rebuild required")
        if args.lane=="C" and previous.get("worker_sha256")!=hashlib.sha256((out/"spike_worker.exe").read_bytes()).hexdigest():
            raise RuntimeError("Worker binary changed; rebuild required")
        report=previous
    report["runner_sha256"]=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
    env=os.environ.copy();env["PATH"]=str(cache)+os.pathsep+env.get("PATH","")
    if args.mode in {"build","all"}:
        command=[str(compiler),"-std=c++20","-O0","-g","-static","-Wall","-Wextra","-Wpedantic","-Werror"]+["-I"+str(root/p) for p in includes]+[str(root/p) for p in sources]+libs+["-o",str(binary)]
        started=time.monotonic()
        run=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,encoding="utf-8",errors="replace")
        (out/"build.log").write_text(run.stdout+run.stderr,encoding="utf-8")
        report["build"]={"exit_code":run.returncode,"seconds":time.monotonic()-started,"command":command}
        print(run.stdout+run.stderr,flush=True)
        if run.returncode:
            (out/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
            return run.returncode
        if args.lane=="C":
            worker=out/"spike_worker.exe"
            worker_command=[part for part in command if not part.endswith("spike_c_tests_main.cpp")]
            worker_command[-1]=str(worker)
            worker_command.insert(-2,str(root/"platform/workers/src/worker_main.cpp"))
            run=subprocess.run(worker_command,cwd=out,env=env,capture_output=True,text=True,encoding="utf-8",errors="replace")
            (out/"worker-build.log").write_text(run.stdout+run.stderr,encoding="utf-8")
            report["worker_build"]={"exit_code":run.returncode,"command":worker_command}
            print(run.stdout+run.stderr,flush=True)
            if run.returncode:
                (out/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
                return run.returncode
            report["worker_sha256"]=hashlib.sha256(worker.read_bytes()).hexdigest()
    if fingerprint()!=manifest:
        raise RuntimeError("Sources changed during build; discard this evidence and rebuild")
    if args.mode in {"test","all"}:
        # Containment writes outside this workspace; protocol-only keeps that gate unmeasured.
        command=[str(binary)]+(["--protocol-only"] if args.lane=="C" else [])
        started=time.monotonic()
        try:
            run=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,encoding="utf-8",errors="replace",timeout=120)
            (out/"test.log").write_text(run.stdout+run.stderr,encoding="utf-8")
            report["tests"]={"exit_code":run.returncode,"seconds":time.monotonic()-started,"command":command}
            print(run.stdout+run.stderr,flush=True)
        except subprocess.TimeoutExpired:
            report["tests"]={"exit_code":124,"seconds":120,"reason":"timeout; not a pass"}
            (out/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
            return 124
    if fingerprint()!=manifest: raise RuntimeError("Sources changed during tests; evidence invalid")
    report["binary_sha256"]=hashlib.sha256(binary.read_bytes()).hexdigest()
    (out/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"lane":args.lane,"report":str(out/"report.json"),"build_exit":report.get("build",{}).get("exit_code"),"test_exit":report.get("tests",{}).get("exit_code"),"full_spike_accepted":False}),flush=True)
    return report.get("tests",report.get("build",{})).get("exit_code",2)

if __name__=="__main__":
    raise SystemExit(main())
