"""Exercise a real local Git clone whose HEAD already equals the pinned revision."""
from pathlib import Path
import importlib.util,json,subprocess
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fetch_dependencies',root/'fetch-dependencies.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def test_fresh_pinned_clone_checks_out_files(tmp_path):
    upstream=tmp_path/'upstream';upstream.mkdir()
    def git(*args):return subprocess.check_output(['git','-C',str(upstream),*args],text=True).strip()
    git('init');git('config','user.name','Host Test');git('config','user.email','host-test@example.invalid')
    (upstream/'source.txt').write_text('Pinned source\n');git('add','source.txt');git('commit','-m','test source')
    commit=git('rev-parse','HEAD');project=tmp_path/'project';project.mkdir()
    (project/'dependencies.json').write_text(json.dumps({'library':{'repository':str(upstream),'commit':commit}}))
    module.fetch_dependencies(project)
    assert (project/'vendor/library/source.txt').read_text()=='Pinned source\n'
    module.fetch_dependencies(project)
    assert (project/'vendor/library/source.txt').exists()
