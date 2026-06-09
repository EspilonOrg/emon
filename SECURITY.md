# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 0.1.x   | Yes       |

## Scope

Issues considered in scope:

- Buffer overflows or memory corruption in serial data parsing
- Race conditions in multi-port monitoring threads
- Signal handler safety issues
- Privilege escalation via log directory handling or daemon mode
- Arbitrary file write through crafted `.pat` pattern files

Out of scope: denial-of-service from a locally-attached device sending malformed data (emon is a developer tool, not a hardened daemon).

## Reporting a Vulnerability

**Do not open a public GitHub issue for security vulnerabilities.**

Email: marouane.elmoufid@ecole2600.com

Include:
- Description of the vulnerability
- Steps to reproduce
- Impact assessment
- Suggested fix (optional)

Expected response time: **72 hours**.

Vulnerabilities will be kept confidential under a **90-day embargo** from the date of acknowledgement. A patch release will be coordinated before public disclosure.
