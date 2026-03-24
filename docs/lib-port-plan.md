# Lib Port Plan: research → lib/cm/cm.c

## Current gap: 327 bytes on alice29 (35,757 lib vs 35,430 research)

## Changes needed (in order of impact):

### 1. Match StateMap (estimated -45 bytes)
- Add `smap_t matchsm` to cm_t struct
- Init: `smap_init(&cm->matchsm, 1<<16); cm->matchsm.rate_n = 900;`
- Replace match_predict in preds[31] with matchSM lookup
- Context: pred_bit(1) × ml_b(3) × cc(2) × prev>>6(2) × partial(4) × bp(4) = 16 bits
- Add update in cm_update

### 2. Sparse Match StateMap (estimated -9 bytes)
- Add `smap_t sparsesm` to cm_t struct
- Init: `smap_init(&cm->sparsesm, 1<<14); cm->sparsesm.rate_n = 900;`
- Replace hardcoded sparse confidence with StateMap lookup
- Context: pred(1) × sl_b(3) × prev>>5(3) × partial(4) × bp(3) = 14 bits

### 3. Models 46-58 (+13 models, estimated -150 bytes)
- Need: N_MODELS 45→58, MAX_INPUTS 56→64
- Model 45-48: o14, sparse order models (sparse13, sparse14, sparse24)
- Model 49-52: triwordmod, sparse match, digram frequency, error history
- Model 53-56: gap (XOR byte-difference), case-insensitive trigram
- Model 57-58: additional order/context models
- Each needs: struct field, init, predict, update, free

### 4. Mixer changes
- mx2: 64→128 entries
- mx6: 128→256 entries  
- mx7: 64→128 entries (add line_pos context)
- mx8: NEW 512 entries (word_length × bp × char_class × match)
- Weights: 7:1:1:2:1:4:2 → 7:1:1:2:1:4:2:8 (add mx8)
- Sum: 18→26
- Cross-term: s1 × s8 × 0.005

### 5. APM changes
- Add parallel APM (apm_par) with word_hash context
- Blend: 1:1:2:28 → 0:1:2:1:28/32
- APM1 still in cascade (feeds APM2) but weight=0 in blend

### 6. LR and parameter changes
- LR: 0.016→0.019, k: 3000→5000
- mx6/mx7/mx8 LR: 0.5×
- T: 1.10 (already in lib)

### 7. Decompress path
- CRITICAL: All predict/update changes must be mirrored exactly
- Lib has separate encode/decode paths — both must match
