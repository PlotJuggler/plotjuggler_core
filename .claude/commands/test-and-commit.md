Validate and commit staged/unstaged changes. Run these steps in order, stopping on first failure:

1. `./build.sh --debug` — Debug+ASAN build must succeed
2. `./test.sh` — all tests must pass
3. `./run_clang_tidy.sh` — no clang-tidy warnings

If all three pass, create a git commit following the repo's commit conventions (see git log for style). If any step fails, fix the issue and re-run from step 1.
