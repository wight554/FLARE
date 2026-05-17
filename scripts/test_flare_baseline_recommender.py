import subprocess
import sys
import os

def test_replay_determinism():
    log_content = """OK: LN:0,BUF:MID,EST:1600.0,BP:-3.0,RT:-3.0
OK: LN:0,BUF:MID,EST:1650.0,BP:-2.0,RT:-3.0
OK: LN:0,BUF:MID,EST:1550.0,BP:-4.0,RT:-3.0
"""
    log_path = "test_stream.log"
    with open(log_path, "w") as fh:
        fh.write(log_content)
        
    cmd = [sys.executable, "scripts/flare_baseline_recommender.py", "--file", log_path]
    out1 = subprocess.check_output(cmd).decode()
    out2 = subprocess.check_output(cmd).decode()
    
    # Remove duration line as it depends on time.time()
    def clean(out):
        lines = [l for l in out.splitlines() if not l.startswith("Duration:")]
        return "\n".join(lines)
        
    assert clean(out1) == clean(out2)
    assert "Suggested baseline_sps: 1600" in out1
    assert "Suggested sync_trailing_bias_frac: 0.400" in out1
    
    os.remove(log_path)
    print("test_replay_determinism PASS")

if __name__ == "__main__":
    test_replay_determinism()
