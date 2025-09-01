# Cashcode

- Spec: `docs/CCBC_SPEC.md`
- VM sources: `cvm/` (builds single binary `cash`)

Build VM:
```
cd cvm
make
```

Create pages:
- Add `.cash` files under `pages/`.
- First line must be: `$route "/path"`
- Rest of the file is HTML for now.

Serve directly from pages (builds in-memory bundle):
```
./cvm/cash serve pages
# Visit http://localhost:3000
```

Serve a prebuilt bundle:
```
./cvm/cash serve build/cash.bundle.ccbc 3000
```

Notes:
- Current MVP renders static HTML per route. `$if/$for` will be compiled by the future source compiler.
- The VM supports HTML ops, branching, and streaming; the in-C bundler emits simple PRINT-based code today for maximal simplicity.

Roadmap (short):
- Parser: `.cash` → CCBC (support `{$expr}`, `$if`, `$for`).
- Actions/Forms: add ops; simple POST handling.
- Caching + headers.
