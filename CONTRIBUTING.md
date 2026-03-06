# Contributing

Thanks for your interest in contributing. You can contribute in many ways, including:

- Reporting bugs
- Suggesting features
- Help in UI language translation
- Writing documentation

This project is actively developed and maintained.
Please read this document carefully before opening an issue or pull request.

---

## Ground rules

- Be concise and technical.
- No drive-by PRs without understanding the codebase.
- If you are unsure about a change, open an issue first.
- Maintain existing style and architecture. Do not refactor unrelated code.

---

## Reporting bugs

Before opening an issue:

1. Make sure the issue is reproducible on the latest `main` branch.
2. Search existing issues to avoid duplicates.

When reporting a bug, include:

- Exact steps to reproduce
- Expected behavior
- Actual behavior
- Platform (OS, compiler, library versions)
- Relevant logs or screenshots if applicable

## Development setup

Clone the repository:

```bash
git clone https://codeberg.org/lektra/lektra.git
cd lektra

mkdir -p build
cmake -S . -B build -DCMAKE_INSTALL_TYPE=Debug
cmake --install build --prefix build/debug
```
