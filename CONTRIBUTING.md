# Contributing to 3DS Google Maps

Thanks for your interest in contributing! This is a Nintendo 3DS homebrew project written in C (C99), built with devkitARM. Please read this guide before opening issues or submitting pull requests.

---

## Table of Contents

- [Getting Started](#getting-started)
- [Building the Project](#building-the-project)
- [Project Structure](#project-structure)
- [How to Contribute](#how-to-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [Requesting Features](#requesting-features)
  - [Submitting Pull Requests](#submitting-pull-requests)
- [Code Style Guidelines](#code-style-guidelines)
- [Architecture Rules — Read Before Coding](#architecture-rules--read-before-coding)
- [Debugging on Device](#debugging-on-device)
- [Things We Won't Accept](#things-we-wont-accept)

---

## Getting Started

You'll need the devkitPro toolchain set up to build this project. There is no way to build it without it — it cross-compiles C to run on the 3DS's ARM11 processor.

1. Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS toolchain
2. Install the required libraries via `dkp-pacman`:
   ```bash
   dkp-pacman -S 3ds-dev
   dkp-pacman -S 3ds-curl 3ds-mbedtls 3ds-libpng
