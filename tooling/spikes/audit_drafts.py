"""Read-only draft scope/header audit; not a substitute for integrated app gates."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

TASKS = ["spike-leader-bootstrap-20261015", "spike-a-viewport-20261015",
         "spike-b-document-20261015", "spike-c-interop-20261015"]

def blob_hash(data):
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--workspace",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    root=args.workspace.resolve()
    modules=json.loads((root/"registry/modules.json").read_text(encoding="utf-8"))["modules"]
    by_prefix={m["path"].split("/")[-1]:m for m in modules}
    report={"method":"Primary Git blob metadata + direct worktree byte hashes; no Git trust changes",
            "text_comparison":"Accept LF/CRLF checkout differences for text without NUL bytes",
            "limitations":"Header namespace scan only; integrated link/dependency/gate review remains pending", "tasks":{}}
    for task_id in TASKS:
        task=json.loads((root/f".agent/tasks/{task_id}.json").read_text(encoding="utf-8"))
        checkout=(root/task["worktree"]["path"]).resolve()
        if not checkout.is_relative_to(root): raise ValueError("Worktree escapes workspace")
        owned=task["owned_paths"]
        raw=subprocess.check_output(["git","-C",str(root),"ls-tree","-r","-z",task["base"]["oid"]])
        tracked={}
        for row in raw.split(b"\0"):
            if not row: continue
            metadata,path=row.split(b"\t",1)
            mode,kind,sha=metadata.split()
            if kind==b"blob": tracked[path.decode()]=sha.decode()
        candidates=set(tracked)
        for directory,dirs,files in os.walk(checkout):
            dirs[:]=[d for d in dirs if d not in {".git",".agent",".worktrees","out","__pycache__"}]
            for name in files:
                if name!=".git" and not name.endswith(".pyc"):
                    candidates.add((Path(directory)/name).relative_to(checkout).as_posix())
        changed=[]; deleted=[]; edges=set(); violations=[]
        for relative in sorted(candidates):
            path=checkout/relative
            if path.is_symlink() or not path.resolve().is_relative_to(checkout):
                raise ValueError("Redirected source "+relative)
            if not path.exists():
                deleted.append(relative); changed.append(relative)
                if path.name=="AGENTS.md": violations.append("Protected instructions deleted: "+relative)
                continue
            content=path.read_bytes()
            if tracked.get(relative) in {blob_hash(content),blob_hash(content.replace(b"\r\n",b"\n")) if b"\0" not in content else ""}:
                continue
            changed.append(relative)
            if path.name=="AGENTS.md": violations.append("Protected instructions changed: "+relative)
            if path.suffix in {".cpp",".hpp"}:
                module=next((m for m in modules if relative.startswith(m["path"]+"/")),None)
                if module:
                    for prefix in re.findall(r'#include\s*[<"]pikdraw/([^/]+)/',content.decode("utf-8")):
                        dependency=by_prefix.get(prefix)
                        if dependency and dependency["id"]!=module["id"]:
                            edge=module["id"]+" -> "+dependency["id"]; edges.add(edge)
                            if dependency["id"] not in module["dependencies"]:
                                violations.append("Undeclared namespace dependency: "+edge)
        for relative in changed:
            if not any(relative==p or relative.startswith(p+"/") for p in owned):
                violations.append("Outside owned paths: "+relative)
        report["tasks"][task_id]={"base_sha":task["base"]["oid"],"worktree":task["worktree"]["path"],
            "changed_paths":changed,"deleted_paths":deleted,"source_namespace_edges":sorted(edges),"violations":violations}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({task:{"changed":len(data["changed_paths"]),"violations":data["violations"],
        "source_namespace_edges":data["source_namespace_edges"]} for task,data in report["tasks"].items()}))
    return 1 if any(t["violations"] for t in report["tasks"].values()) else 0

if __name__=="__main__": raise SystemExit(main())
