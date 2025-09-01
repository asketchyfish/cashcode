# Cashcode

- Spec: `docs/CCBC_SPEC.md`
- VM sources: `cvm/` (builds single binary `cash`)

Build VM:
```
cd cvm
make
# optional: set version
# make VERSION=0.2.0
# install system-wide (optional)
# sudo make install
```

Create pages:
- Add `.cash` files under `pages/`.
- First line must be: `$route "/path"`
- Rest of the file is HTML for now.

Serve directly from a directory (builds in-memory bundle):
```
./cvm/cash dev .            # default port 3000
# or set a port
./cvm/cash dev . 4000
# Visit http://localhost:3000
```

Serve a prebuilt bundle:
```
./cvm/cash serve build/cash.bundle.ccbc 3000
```

Version:
```
./cvm/cash --version
```

Notes:
- Current MVP renders static HTML per route. `$if/$for` will be compiled by the future source compiler.
- The VM supports HTML ops, branching, and streaming; the in-C bundler emits simple PRINT-based code today for maximal simplicity.

Roadmap (short):
- Parser: `.cash` → CCBC (support `{$expr}`, `$if`, `$for`).
- Actions/Forms: add ops; simple POST handling.
- Caching + headers.
