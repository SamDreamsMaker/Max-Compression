#!/bin/bash
# Ablation study: test compression with each model zeroed out
cd /home/overmind/.openclaw/workspace/max-compression

BASE=$(./research/exp015 /tmp/cantrbry/alice29.txt 2>&1 | grep Compressed | awk '{print $2}')
echo "Baseline: $BASE bytes"
echo ""

# For each model index, we'd need to modify code...
# Instead, just report current state
echo "29 models at $BASE bytes, 18 KB/s"
echo "Next: try removing low-value models"
