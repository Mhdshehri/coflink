# Security policy

CoFLink relays game traffic over the internet between Steam friends, so security matters. Thank you for helping keep its players safe.

## Supported versions

Only the latest release gets security fixes. Please update before you report a problem.

| Version | Supported |
|---|---|
| Latest release | ✅ |
| Older versions | ❌ |

## Reporting a vulnerability

**Please do not open a public issue for a security problem.** A public report shows everyone how to use the problem before it is fixed.

Report it privately instead:

1. Go to the [Security tab](../../security) of this repository.
2. Click **Report a vulnerability**.
3. Describe what you found, how to reproduce it, and what an attacker could do with it.

You can expect a first reply within a few days. Once the problem is confirmed, a fixed release is published and you are credited, unless you prefer not to be.

## What counts

Examples of things worth reporting:

- someone who is **not** the host's Steam friend getting into the host's game
- a way to learn the other player's IP address through CoFLink
- a crafted packet or Steam message that crashes CoFLink or makes it misbehave
- a way to make CoFLink load a different `steam_api.dll` than the pinned one

Not CoFLink's to fix:

- bugs in Cry of Fear itself or its server, which CoFLink passes traffic to unchanged
- problems in Steam or in Valve's relay network
- Windows SmartScreen or antivirus warnings about the unsigned `CoFLink.exe` (see the README for how to check the file)

## Verifying a download

Every release lists the SHA-256 of `CoFLink.exe`, and the zip includes `SHA256SUMS.txt`. The build is reproducible: building the source of a release gives exactly the same file. See the README for details.
