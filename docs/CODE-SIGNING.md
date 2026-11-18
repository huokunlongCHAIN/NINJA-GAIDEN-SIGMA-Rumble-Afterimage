# Code signing policy

## Status

An application to the SignPath Foundation was submitted on 2026-11-18; the
application page displayed "Form submitted". Approval is pending. No subscription,
certificate, signed release, or approval is claimed. Current test packages are
unsigned. A valid signature would identify the publisher and protect file
integrity; it would not guarantee antivirus or Nexus approval.

The intended provider is [SignPath.io](https://signpath.io/) using a certificate
issued to [SignPath Foundation](https://signpath.org/). The required attribution
will be activated only if signing is approved and actually provided.

## Responsibilities

- Author/committer and reviewer: [huokunlongCHAIN](https://github.com/huokunlongCHAIN).
- Proposed release signing approver: huokunlongCHAIN.
- External contributions require maintainer review before merging.
- Every signing request requires manual maintainer approval. GitHub and SignPath
  multi-factor authentication must be enabled before production signing access.

## Source and build provenance

The Windows workflow builds the native plugin, optional crash tracing helper,
and installer from this repository on GitHub-hosted Windows runners. Action
revisions are pinned. Unsigned artifacts and their SHA-256 checksums are uploaded
for inspection. No game files, private keys, credentials, or signing requests are
included. CI artifacts are test builds, not automatically published releases.

All three shipped binaries have the same product name and product version. If
SignPath approves the application, signing must occur in this order: sign the
plugin and helper, regenerate the installer's component hashes from those signed
files, compile and sign the installer, then generate final package checksums.
The existing local-certificate build follows that ordering. Service integration
is pending approval and will be reviewed separately; the current CI does not
claim to perform it.

## Behavior and review

The native-plugin test loads through the game's DirectInput import and forwards
Windows input APIs. It hooks game/input/render functions inside that same process
to implement afterimages, rumble, and controller support. The installer copies
verified files beside the selected game EXE, records backups, and offers full
restoration. It does not modify game executables or archives.

There is no resident injector, new startup registration, antivirus exclusion,
security disabling, obfuscation, or installed trust root. Optional crash tracing
uses a separate visible Windows debugger helper only on user request. Legacy
cleanup may remove only owned older autoloader entries. See
[SECURITY-REVIEW.txt](../release/SECURITY-REVIEW.txt) for details and limits.

Some antivirus products still flag current unsigned test artifacts. These reports
are disclosed for review, not asserted to be proven false positives. Older public
releases use an external injector and are not candidates for this application.
Signing approval is requested for the native-plugin architecture only.

## Privacy

See [Privacy policy](PRIVACY.md). The software does not automatically transmit
logs, diagnostics, telemetry, or game/controller data to network services.
