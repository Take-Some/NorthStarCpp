"""Validate reusable Abby .nprefab as animated NPC, no possession or weapon."""
from pathlib import Path
import sys
from abby_pistol_demo_runtime_smoke import ROOT,run

if __name__=="__main__":
    player=Path(sys.argv[1] if len(sys.argv)>1 else
        ROOT/"_qa_editor_mixed"/"northstarcpp_player.exe")
    run(player,no_weapon=True)
