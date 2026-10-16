<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

---

## [1.0.0] – 2026-10-19

Initial public release, developed under the ElaadNL
[Residential Flexibility](https://elaad.nl/en/projects/residential-flexibility/)
initiative.

### Added

- ESP-IDF library implementing an S2 Resource Manager (RM)
- [S2 Connect v1.0](https://docs.s2standard.org/s2-connect/1.0.0/discovery-pairing-authentication/) pairing, connection, unpairing, and DNS-SD discovery of the CEM
- HMAC challenge-response authentication with TLS certificate fingerprint pinning
- Long polling for Resource Managers without an HTTP server
- Power Envelope Based Control (PEBC) control type, including measurements and forecasts
- NVS persistence of access tokens and the pinned TLS fingerprint
- Basic example and unit tests
