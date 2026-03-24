# Security Policy

## Supported Versions

| Version | Supported          |
|---------|--------------------|
| 2.2.x   | ✅ Current          |
| 2.1.x   | ✅ Security fixes   |
| < 2.1   | ❌ End of life      |

## Reporting a Vulnerability

If you discover a security vulnerability in MaxCompression, please report it responsibly:

1. **Do NOT** open a public GitHub issue.
2. Email: `contact@dreams-makers.com`
3. Include: MCX version, steps to reproduce, impact assessment.

We aim to respond within 48 hours and provide a fix within 7 days for critical issues.

## Scope

Security-relevant areas include:
- **Buffer overflows** in decompression (malformed input handling)
- **Integer overflows** in size calculations
- **Denial of service** via crafted compressed data (excessive memory/CPU)
- **Out-of-bounds reads** during decompression

## Known Mitigations

- All decompressor paths validate frame headers before processing
- Block sizes are bounded by `MCX_MAX_BLOCK_SIZE` (64 MB)
- Decompressed size is read from frame header, limiting output allocation
- Malformed input test suite verifies graceful error handling
