"""Reproducible Release document measurement, independent of renderer acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import zipfile

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--workspace",type=Path,required=True)
    ap.add_argument("--candidate",type=Path,required=True)
    ap.add_argument("--toolchain",type=Path,required=True)
    ap.add_argument("--baseline",help="Export this committed revision instead of using candidate sources")
    args=ap.parse_args()
    workspace=args.workspace.resolve(); candidate=args.candidate.resolve()
    out=Path(__file__).resolve().parents[2]/"out/document-benchmarks"/str(time.time_ns())
    out.mkdir(parents=True)
    source=candidate
    if args.baseline:
        archive=out/"baseline.zip"
        subprocess.run(["git","-C",str(workspace),"archive","--format=zip","--output",str(archive),args.baseline,"core/geometry","core/document"],check=True)
        source=out/"source";source.mkdir()
        with zipfile.ZipFile(archive) as zipped:
            for info in zipped.infolist():
                if not (source/info.filename).resolve().is_relative_to(source): raise ValueError("Unsafe baseline archive")
            zipped.extractall(source)
    includes=[source/"core/document/include",source/"core/geometry/include"]
    files=[source/"core/geometry/src"/p for p in ["transform.cpp","rect.cpp","curve.cpp","boolean_offset.cpp"]]
    files += [source/"core/document/src"/p for p in ["snapshot.cpp","document.cpp","persistence.cpp"]]
    files += [candidate/"core/document/tests/benchmark_document.cpp"]
    inputs=set(files)
    for directory in includes: inputs.update(directory.rglob("*.hpp"))
    def hashes(): return {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(inputs)}
    before=hashes()
    binary=out/"benchmark.exe"
    command=[str(args.toolchain.resolve()/"bin/clang++.exe"),"-std=c++20","-O2","-DNDEBUG","-static","-Wall","-Wextra","-Wpedantic","-Werror"]
    if not args.baseline: command += ["-DPIKDRAW_BENCHMARK_PHASES=1"]
    command += ["-I"+str(p) for p in includes]+[str(p) for p in files]+["-lpsapi","-o",str(binary)]
    build=subprocess.run(command,cwd=out,capture_output=True,text=True,encoding="utf-8",errors="replace")
    (out/"build.log").write_text(build.stdout+build.stderr,encoding="utf-8")
    if build.returncode: print(build.stdout+build.stderr);return build.returncode
    run=subprocess.run([str(binary)],cwd=out,capture_output=True,text=True,timeout=180)
    (out/"run.log").write_text(run.stdout+run.stderr,encoding="utf-8")
    if run.returncode: print(run.stdout+run.stderr);return run.returncode
    if before!=hashes(): raise RuntimeError("Inputs changed while compiling/running; repeat this measurement")
    result={"source_revision":args.baseline or "candidate working tree", "inputs":before,"command":command,
        "binary_sha256":hashlib.sha256(binary.read_bytes()).hexdigest(),"measurement":json.loads(run.stdout),
        "limits":"One local Release run; no concurrent renderer, UI latency, production corpus or acceptance threshold"}
    (out/"report.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"report":str(out/"report.json"),"measurement":{key:value for key,value in result["measurement"].items() if key not in {"samples_sorted_ms","samples_ms","phase_samples_us"}}}))
    return 0

if __name__=="__main__":raise SystemExit(main())
