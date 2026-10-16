# Security policy

## Reporting a vulnerability

Please report suspected security vulnerabilities privately rather than publishing exploit details in a public issue.

Use GitHub's **Private vulnerability reporting** feature for this repository if it is enabled. If it is unavailable, contact the repository owner through their GitHub profile and request a private channel. Include the affected version or commit, impact, and minimal reproduction steps. Do not include credentials, access tokens, private data, or exploit payloads that affect other users.

## Supported versions

Security fixes are considered for the latest code on the `master` branch. No formal long-term-support policy is established unless a release explicitly says otherwise.

## Scope and limitations

VantaOS is experimental and has not necessarily undergone an independent security audit or penetration test. It is not suitable for sensitive workloads. In particular, kernel-mode process execution and incomplete memory-permission enforcement mean that a verified user-mode isolation boundary is not available. No software can be guaranteed free of vulnerabilities.

Please allow time to investigate and prepare a fix before public disclosure. This policy does not promise a specific response time or bounty.
