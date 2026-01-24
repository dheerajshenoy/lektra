# Contributing

Thanks for your interest in contributing. This project is actively developed and maintained.
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

---

## Feature requests

Feature requests should explain:

- The problem you are trying to solve
- Why existing functionality is insufficient
- Why the feature belongs in this project

"I want X" without justification is not enough.

---

## Development setup

Clone the repository:

```bash
git clone https://codeberg.org/lektra/lektra.git
cd lektra
```

### Building

```bash
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_INSTALL_TYPE=Debug
ninja && ./lektra
```
