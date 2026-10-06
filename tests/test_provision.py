from pathlib import Path
import importlib.util,pytest
ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('provision',ROOT/'provision.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
def test_missing_key_never_generates_or_rotates(tmp_path,monkeypatch):
    monkeypatch.delenv('MINI_MASTER_KEY_HEX',raising=False)
    with pytest.raises(ValueError):p.load_master_key(tmp_path)
    assert not (tmp_path/'.env').exists()
def test_env_and_file_selection_and_rotation_guard(tmp_path,monkeypatch):
    monkeypatch.delenv('MINI_MASTER_KEY_HEX',raising=False)
    (tmp_path/'.env').write_text('MINI_MASTER_KEY_HEX='+'01'*32)
    key=p.load_master_key(tmp_path);p.write_provision(tmp_path,key)
    p.write_provision(tmp_path,key)
    monkeypatch.setenv('MINI_MASTER_KEY_HEX','02'*32)
    with pytest.raises(ValueError):p.write_provision(tmp_path,p.load_master_key(tmp_path))
    monkeypatch.setenv('MINI_MASTER_KEY_HEX','bad')
    with pytest.raises(ValueError):p.load_master_key(tmp_path)
