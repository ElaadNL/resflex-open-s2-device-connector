<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Contributing Guide

Thank you for your interest in contributing to this project.

This repository contains the S2 device library: an S2 Resource Manager
implementation for ESP-IDF, developed as part of the ElaadNL
[Residential Flexibility](https://elaad.nl/en/projects/residential-flexibility/)
initiative.

---

## Scope of Contributions

You can contribute by:

- Reporting bugs or interoperability issues with S2 Customer Energy Managers
- Improving the S2 Connect pairing, connection, and PEBC implementation
- Adding or improving tests
- Improving documentation and examples

Generic S2 protocol behaviour belongs in this library. Product-specific logic,
such as device control or Modbus mapping, belongs in the application that uses
the library, for example
[`resflex-open-modbus-converter`](https://github.com/ElaadNL/resflex-open-modbus-converter).

---

## How to Contribute

1. Fork the repository
2. Create a focused branch for your change:

   ```bash
   git checkout -b fix/my-change
   ```

3. Make a small, reviewable change and add or update tests where reasonable
4. Run the linting and tests described in the [README](README.md#development)
5. Open a pull request against the `main` branch with a clear description of
   what changed, why, and how you validated it

---

## Review and Merge Process

All changes are reviewed by ElaadNL maintainers. Pull requests are reviewed for:

- Correct behaviour according to the S2 and S2 Connect specifications
- Security of pairing, token handling, and network input parsing
- Memory safety and resource use on ESP32 targets
- Adequate tests and documentation

---

## Branch Naming Convention

- `feat/<area>` – new features
- `fix/<description>` – bug fixes
- `test/<description>` – test-only changes
- `docs/<topic>` – documentation changes
- `chore/<area>` – maintenance and dependency updates

---

## License and Copyright

By contributing, you agree that your contribution is licensed under the
[Apache License 2.0](LICENSE). New files must carry REUSE-compliant copyright
and license information (an SPDX header, a `.license` sidecar file, or a matching
rule in `REUSE.toml`).

No Contributor License Agreement (CLA) is required.

---

## Code of Conduct

Please keep all contributions respectful and constructive.

---

## Security

Do not report security issues in public issues. Follow the process in
[SECURITY.md](SECURITY.md).

---

## Questions?

See [SUPPORT.md](SUPPORT.md), or open an issue if you are unsure whether
something belongs in this repository.
