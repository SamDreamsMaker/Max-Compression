# MCX Autoresearch Program

## Goal
Find breakthrough compression improvements through autonomous experimentation.
The metric is **bits per byte (bpb)** on a fixed test corpus. Lower = better.

## Test Corpus (fast — <2s total on Atom CPU)
- `/tmp/cantrbry/alice29.txt` (152,089 bytes) — English text
- `/tmp/silesia/ooffice` (6,152,192 bytes) — x86 binary
- `/tmp/silesia/nci` (33,553,445 bytes) — chemical data

## Baseline (v2.2.0 L20)
- alice29: 43154 bytes → 2.271 bpb
- ooffice: 2405901 bytes → 3.128 bpb  
- nci: 1304238 bytes → 0.311 bpb
- **Composite: 1.903 bpb** (weighted average by original size)

## Rules
1. All compression MUST be lossless (roundtrip test required)
2. Each experiment modifies ONE thing
3. Measure on all 3 files
4. Keep if composite bpb improves, revert if not
5. Log EVERYTHING to results.tsv
6. Commit improvements with clear description

## What to explore
Anything in lib/ is fair game. Focus on approaches that NO existing 
compressor uses. Think beyond LZ/BWT/ANS.
