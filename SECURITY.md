# Security policy

## Supported line

Security fixes are accepted for the current Thunder 1.0 development line.

## Reporting

Please report suspected vulnerabilities privately through the repository's
GitHub Security Advisory page. Do not include credentials, private save files,
commercial-game assets, or other sensitive data in a public issue.

Useful reports include the affected revision, platform/compiler, minimal
reproduction, expected behavior, and whether malformed content, save files,
network input, or shader/assets are involved.

## Scope

Important security boundaries include parsers, mod/content loading, save and
world-pack decoding, archive/path handling, external tooling, and GPU resource
creation. A crash caused only by intentionally invalid internal C++ API usage
may be treated as a correctness issue rather than a security vulnerability.
